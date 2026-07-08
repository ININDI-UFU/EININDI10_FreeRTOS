#include <Arduino.h>
#include <esp_timer.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "services/wserial.h"
#include "dsps_biquad.h"

// Aquisicao real no GPIO34 @ 100 Hz, dividida em tasks FreeRTOS distintas:
//   [TaskSampling] -> fila rawQueue -> [TaskFilter] -> fila filteredQueue -> [TaskPublish]
// Uma quarta task [TaskStats] mede periodicamente o desempenho de cada task
// (tempo de execucao min/media/max, stack livre) e o consumo de memoria (heap),
// imprimindo tudo pela serial (wserial/Teleplot).

constexpr uint8_t ADC_PIN = 34;
constexpr uint32_t SERIAL_BAUD = 115200;

constexpr float SAMPLE_RATE_HZ = 100.0f;
constexpr uint32_t SAMPLE_PERIOD_MS = static_cast<uint32_t>(1000.0f / SAMPLE_RATE_HZ);
constexpr float ADC_MAX_COUNTS = 4095.0f;
constexpr float ADC_REF_VOLTS = 3.3f;

constexpr uint32_t STATS_PERIOD_MS = 2000;

// Liga um sinal sintetico (10 Hz + 40 Hz + 110 Hz) no lugar do analogRead().
// Usado para validar o pipeline FreeRTOS/filtro no SimulIDE, cujo modelo de
// ADC do ESP32 nao repassa a tensao do circuito pro periferico simulado
// (analogRead sempre retorna 0 la, independente do que esta ligado no pino).
#define USE_SYNTHETIC_ADC 1

// ---------------------------------------------------------------------------
// Filtro: Butterworth passa-baixa de 4a ordem, fs=100 Hz, fc=20 Hz.
// Esta frequencia de corte deixa 10 Hz passar bem e atenua fortemente 40 Hz.
// Coeficientes no formato exigido por dsps_biquad_f32:
// [b0, b1, b2, a1, a2], com a0 = 1.
// ---------------------------------------------------------------------------
static float lowpass_stage1_coeffs[5] = {
    0.029058182199822265f,
    0.05811636439964453f,
    0.029058182199822265f,
    -0.5159313372981117f,
    0.1011974058740317f,
};

static float lowpass_stage2_coeffs[5] = {
    1.0f,
    2.0f,
    1.0f,
    -0.7002830914282003f,
    0.49467548859626914f,
};

static float lowpass_stage1_state[2] = {0.0f, 0.0f};
static float lowpass_stage2_state[2] = {0.0f, 0.0f};

#if USE_SYNTHETIC_ADC
// Mistura 10 Hz (deve passar) + 40 Hz (deve ser atenuado) + 110 Hz (alias de
// 10 Hz ao amostrar a 100 Hz), centrada em ADC_REF_VOLTS/2 pra imitar a faixa
// do ADC real.
//
// Tabela de 1 periodo fundamental (100 ms = mmc de 10/40/110 Hz a 100 Hz =
// 10 amostras), calculada uma unica vez no boot com indice i pequeno
// (0..9) -- dsps_tone_gen_f32 foi tentado aqui, mas sua faixa valida de
// frequencia e' -1..1 (normalizado por fs) e 110 Hz corresponde a 1.1, fora
// da faixa: a amplitude gerada saiu incorreta. Evitamos tambem sinf(2*PI*f*t)
// com t = tempo desde o boot, pois apos alguns minutos de uptime o argumento
// chega a dezenas de milhares de radianos, faixa em que sinf() perde precisao.
// Usando i (0..9) direto no lugar de t o argumento nunca cresce.
constexpr int SYNTH_TABLE_LEN = 10;
static float synthTable[SYNTH_TABLE_LEN];

void synthTableInit() {
    for (int i = 0; i < SYNTH_TABLE_LEN; i++) {
        const float ph = 2.0f * PI * static_cast<float>(i) / SAMPLE_RATE_HZ;
        synthTable[i] = 0.5f * sinf(ph * 10.0f)
                       + 0.3f * sinf(ph * 40.0f)
                       + 0.2f * sinf(ph * 110.0f);
    }
}

float readAdcVolts() {
    static uint32_t idx = 0;
    return (ADC_REF_VOLTS / 2.0f) + synthTable[idx++ % SYNTH_TABLE_LEN];
}
#else
float readAdcVolts() {
    const uint16_t counts = analogRead(ADC_PIN);
    return (static_cast<float>(counts) * ADC_REF_VOLTS) / ADC_MAX_COUNTS;
}
#endif

float filterLowpassCascade(float sample) {
    float stage1 = 0.0f;
    float filtered = 0.0f;

    dsps_biquad_f32(&sample, &stage1, 1, lowpass_stage1_coeffs, lowpass_stage1_state);
    dsps_biquad_f32(&stage1, &filtered, 1, lowpass_stage2_coeffs, lowpass_stage2_state);

    return filtered;
}

// ---------------------------------------------------------------------------
// Estruturas trocadas entre tasks via fila (queue)
// ---------------------------------------------------------------------------
struct RawSample {
    float raw;
    int64_t t_us; // instante da aquisicao (esp_timer_get_time), usado p/ latencia
};

struct FilteredSample {
    float raw;
    float filtered;
    int64_t t_us;
};

// ---------------------------------------------------------------------------
// Medicao de desempenho por task: tempo de execucao (min/media/max) em us.
// Protegido por spinlock, pois e escrito pela propria task e lido pela TaskStats.
// ---------------------------------------------------------------------------
struct TaskTiming {
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    uint32_t lastUs = 0;
    uint32_t minUs = UINT32_MAX;
    uint32_t maxUs = 0;
    uint64_t sumUs = 0;
    uint32_t samples = 0;
    uint32_t overruns = 0; // ex.: fila cheia, amostra descartada

    void record(uint32_t execUs) {
        portENTER_CRITICAL(&mux);
        lastUs = execUs;
        if (execUs < minUs) minUs = execUs;
        if (execUs > maxUs) maxUs = execUs;
        sumUs += execUs;
        samples++;
        portEXIT_CRITICAL(&mux);
    }

    void addOverrun() {
        portENTER_CRITICAL(&mux);
        overruns++;
        portEXIT_CRITICAL(&mux);
    }

    // Copia atomica dos contadores e zera min/max/soma para a proxima janela.
    void snapshot(uint32_t &outMin, uint32_t &outAvg, uint32_t &outMax, uint32_t &outOverruns) {
        portENTER_CRITICAL(&mux);
        outMin = (samples == 0) ? 0 : minUs;
        outAvg = (samples == 0) ? 0 : static_cast<uint32_t>(sumUs / samples);
        outMax = maxUs;
        outOverruns = overruns;
        minUs = UINT32_MAX;
        maxUs = 0;
        sumUs = 0;
        samples = 0;
        portEXIT_CRITICAL(&mux);
    }
};

static TaskTiming samplingTiming;
static TaskTiming filterTiming;
static TaskTiming publishTiming;

// ---------------------------------------------------------------------------
// Handles globais de FreeRTOS
// ---------------------------------------------------------------------------
static QueueHandle_t rawQueue = nullptr;
static QueueHandle_t filteredQueue = nullptr;
static SemaphoreHandle_t serialMutex = nullptr;

static TaskHandle_t samplingTaskHandle = nullptr;
static TaskHandle_t filterTaskHandle = nullptr;
static TaskHandle_t publishTaskHandle = nullptr;
static TaskHandle_t statsTaskHandle = nullptr;

constexpr UBaseType_t RAW_QUEUE_LEN = 4;
constexpr UBaseType_t FILTERED_QUEUE_LEN = 4;

// ---------------------------------------------------------------------------
// TaskSampling: le o ADC a 100 Hz (vTaskDelayUntil garante o periodo exato,
// sem acumular deriva) e publica a amostra bruta na fila.
// ---------------------------------------------------------------------------
void taskSampling(void *pvParameters) {
    const TickType_t period = pdMS_TO_TICKS(SAMPLE_PERIOD_MS);
    TickType_t lastWake = xTaskGetTickCount();

    for (;;) {
        const int64_t t0 = esp_timer_get_time();

        RawSample sample;
        sample.raw = readAdcVolts();
        sample.t_us = t0;

        if (xQueueSend(rawQueue, &sample, 0) != pdPASS) {
            // Fila cheia: descarta a amostra mais antiga para manter o dado mais recente.
            RawSample discard;
            xQueueReceive(rawQueue, &discard, 0);
            xQueueSend(rawQueue, &sample, 0);
            samplingTiming.addOverrun();
        }

        const uint32_t execUs = static_cast<uint32_t>(esp_timer_get_time() - t0);
        samplingTiming.record(execUs);

        vTaskDelayUntil(&lastWake, period);
    }
}

// ---------------------------------------------------------------------------
// TaskFilter: bloqueia na fila de entrada, aplica o filtro biquad em cascata
// e publica o resultado na fila de saida.
// ---------------------------------------------------------------------------
void taskFilter(void *pvParameters) {
    RawSample sample;

    for (;;) {
        if (xQueueReceive(rawQueue, &sample, portMAX_DELAY) != pdPASS) continue;

        const int64_t t0 = esp_timer_get_time();
        const float filtered = filterLowpassCascade(sample.raw);
        const uint32_t execUs = static_cast<uint32_t>(esp_timer_get_time() - t0);
        filterTiming.record(execUs);

        FilteredSample out{sample.raw, filtered, sample.t_us};
        if (xQueueSend(filteredQueue, &out, 0) != pdPASS) {
            filterTiming.addOverrun();
        }
    }
}

// ---------------------------------------------------------------------------
// TaskPublish: bloqueia na fila filtrada e envia o par (raw, filtered) pela
// serial, junto da latencia fim-a-fim (amostra -> publicacao).
// ---------------------------------------------------------------------------
void taskPublish(void *pvParameters) {
    FilteredSample sample;

    for (;;) {
        if (xQueueReceive(filteredQueue, &sample, portMAX_DELAY) != pdPASS) continue;

        const int64_t t0 = esp_timer_get_time();
        const uint32_t latencyUs = static_cast<uint32_t>(t0 - sample.t_us);

        xSemaphoreTake(serialMutex, portMAX_DELAY);
        wserial.plot("raw", sample.raw, "V");
        wserial.plot("filtered", sample.filtered, "V");
        wserial.plot("latencia", static_cast<float>(latencyUs), "us");
        xSemaphoreGive(serialMutex);

        const uint32_t execUs = static_cast<uint32_t>(esp_timer_get_time() - t0);
        publishTiming.record(execUs);
    }
}

// ---------------------------------------------------------------------------
// TaskStats: a cada STATS_PERIOD_MS imprime desempenho (tempo de execucao das
// tasks, backlog das filas) e memoria (heap livre/minimo, stack livre por task).
// ---------------------------------------------------------------------------
void printTaskLine(const char *name, TaskHandle_t handle, TaskTiming &timing) {
    uint32_t minUs, avgUs, maxUs, overruns;
    timing.snapshot(minUs, avgUs, maxUs, overruns);

    const UBaseType_t hwmWords = (handle != nullptr) ? uxTaskGetStackHighWaterMark(handle) : 0;
    const uint32_t hwmBytes = static_cast<uint32_t>(hwmWords) * sizeof(StackType_t);

    xSemaphoreTake(serialMutex, portMAX_DELAY);
    wserial.print("  - ");
    wserial.print(name);
    wserial.print(": exec(us) min/media/max=");
    wserial.print(minUs);
    wserial.print("/");
    wserial.print(avgUs);
    wserial.print("/");
    wserial.print(maxUs);
    wserial.print("  stack_livre=");
    wserial.print(hwmBytes);
    wserial.print("B  overruns=");
    wserial.println(overruns);
    xSemaphoreGive(serialMutex);
}

void taskStats(void *pvParameters) {
    const TickType_t period = pdMS_TO_TICKS(STATS_PERIOD_MS);
    TickType_t lastWake = xTaskGetTickCount();
    const TaskHandle_t selfHandle = xTaskGetCurrentTaskHandle();

    for (;;) {
        vTaskDelayUntil(&lastWake, period);

        const UBaseType_t rawBacklog = uxQueueMessagesWaiting(rawQueue);
        const UBaseType_t filteredBacklog = uxQueueMessagesWaiting(filteredQueue);

        xSemaphoreTake(serialMutex, portMAX_DELAY);
        wserial.println("---- FreeRTOS stats ----");
        wserial.print("uptime_ms="); wserial.print(millis());
        wserial.print("  heap_livre="); wserial.print(ESP.getFreeHeap());
        wserial.print("B  heap_min="); wserial.print(ESP.getMinFreeHeap());
        wserial.print("B  tasks="); wserial.print((uint32_t)uxTaskGetNumberOfTasks());
        wserial.print("  fila_raw="); wserial.print((uint32_t)rawBacklog);
        wserial.print("/"); wserial.print((uint32_t)RAW_QUEUE_LEN);
        wserial.print("  fila_filt="); wserial.print((uint32_t)filteredBacklog);
        wserial.print("/"); wserial.println((uint32_t)FILTERED_QUEUE_LEN);

        wserial.plot("heap_livre", static_cast<float>(ESP.getFreeHeap()), "B");
        xSemaphoreGive(serialMutex);

        printTaskLine("sampling", samplingTaskHandle, samplingTiming);
        printTaskLine("filter", filterTaskHandle, filterTiming);
        printTaskLine("publish", publishTaskHandle, publishTiming);

        const UBaseType_t statsHwmWords = uxTaskGetStackHighWaterMark(selfHandle);
        xSemaphoreTake(serialMutex, portMAX_DELAY);
        wserial.print("  - stats: stack_livre=");
        wserial.print((uint32_t)statsHwmWords * sizeof(StackType_t));
        wserial.println("B");
        xSemaphoreGive(serialMutex);
    }
}

void setup() {
    wserial.begin(SERIAL_BAUD);
    delay(1000);
#if USE_SYNTHETIC_ADC
    synthTableInit();
#else
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    pinMode(ADC_PIN, INPUT);
#endif
    wserial.println("GPIO34 @ 100 Hz: sampling/filter/publish em tasks FreeRTOS distintas");

    serialMutex = xSemaphoreCreateMutex();
    rawQueue = xQueueCreate(RAW_QUEUE_LEN, sizeof(RawSample));
    filteredQueue = xQueueCreate(FILTERED_QUEUE_LEN, sizeof(FilteredSample));

    // Sampling e Filter no core 1 (isolados do stack de WiFi/BT no core 0),
    // com a Sampling em prioridade mais alta para preservar o periodo de 10 ms.
    xTaskCreatePinnedToCore(taskSampling, "Sampling", 2048, nullptr, 3, &samplingTaskHandle, 1);
    xTaskCreatePinnedToCore(taskFilter, "Filter", 3072, nullptr, 2, &filterTaskHandle, 1);

    // Publish e Stats no core 0: nao sao criticos em tempo, so consomem as filas.
    xTaskCreatePinnedToCore(taskPublish, "Publish", 3072, nullptr, 1, &publishTaskHandle, 0);
    xTaskCreatePinnedToCore(taskStats, "Stats", 4096, nullptr, 1, &statsTaskHandle, 0);
}

void loop() {
    // O trabalho fica todo nas tasks; o loop padrao do Arduino so mantem
    // a serial (comandos/reconexao UDP) e cede tempo de CPU.
    wserial.update();
    vTaskDelay(pdMS_TO_TICKS(10));
}
// touch
