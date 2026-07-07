#pragma once
/**
 * @file AdcDmaEsp.h
 * @brief ADC em modo contínuo com DMA para ESP32-S3.
 *
 * O driver adc_continuous do ESP-IDF v5 configura o hardware para amostrar
 * o ADC de forma autônoma e depositar os resultados num buffer circular via
 * DMA — sem nenhuma intervenção da CPU a cada amostra.
 * Quando o buffer atinge o limiar (conv_frame_size), uma ISR sinaliza que
 * há um "frame" pronto para ser drenado pelo loop() principal.
 *
 * ┌──────────────────────────────────────────────────────────────────────┐
 * │  Fluxo de dados                                                      │
 * │                                                                      │
 * │  ADC ──(DMA)──► buffer circular ──► frame completo ──► ISR flag     │
 * │                                                      (seta _ready)  │
 * │  loop():  available()? ──► read() decodifica ──► wserial.plot()     │
 * └──────────────────────────────────────────────────────────────────────┘
 *
 * Uso mínimo (1 canal):
 * @code
 *   adcDma.begin(ADC_CHANNEL_3, 20000);  // GPIO4 no ESP32-S3, 20 kHz
 *   adcDma.start();
 *
 *   AdcDmaSample buf[ADC_DMA_SAMPLES_PER_FRAME];
 *   void loop() {
 *       if (adcDma.available()) {
 *           size_t n = adcDma.read(buf, ADC_DMA_SAMPLES_PER_FRAME);
 *           for (size_t i = 0; i < n; i++)
 *               wserial.plot("adc", buf[i].value, "counts");
 *       }
 *   }
 * @endcode
 *
 * Mapa de pinos ADC1 no ESP32-S3:
 *   CH0→GPIO1  CH1→GPIO2  CH2→GPIO3  CH3→GPIO4
 *   CH4→GPIO5  CH5→GPIO6  CH6→GPIO7  CH7→GPIO8
 *   CH8→GPIO9  CH9→GPIO10
 *
 * @note Requer Arduino-ESP32 >= 3.0.0 (espressif32 @ ^6.0.0 no platformio.ini).
 * @note Apenas ADC1 pode ser usado com DMA quando o WiFi está ativo.
 * @note O formato de saída TYPE2 é específico do ESP32-S3/S2.
 */

#include <Arduino.h>
#include <esp_adc/adc_continuous.h>  ///< Driver de ADC contínuo do ESP-IDF v5

// ── Parâmetros ajustáveis via #define antes do #include ──────────────────────

#ifndef ADC_DMA_MAX_CHANNELS
/** Máximo de canais monitorados simultaneamente. */
#define ADC_DMA_MAX_CHANNELS 4
#endif

#ifndef ADC_DMA_FRAME_SIZE
/**
 * Tamanho em bytes de um frame de conversão.
 * Deve ser múltiplo de 4 (SOC_ADC_DIGI_DATA_BYTES_PER_CONV no ESP32-S3).
 * 256 bytes → 64 amostras por frame (cada amostra = 4 bytes).
 */
#define ADC_DMA_FRAME_SIZE 256
#endif

#ifndef ADC_DMA_POOL_SIZE
/**
 * Tamanho do buffer circular DMA interno (bytes).
 * Deve ser >= ADC_DMA_FRAME_SIZE. Buffer maior = mais tolerância a atrasos
 * no loop() sem perder amostras.
 */
#define ADC_DMA_POOL_SIZE 1024
#endif

/** Número de amostras (AdcDmaSample) em um frame completo. */
#define ADC_DMA_SAMPLES_PER_FRAME (ADC_DMA_FRAME_SIZE / 4)

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Amostra ADC decodificada.
 *
 * O driver retorna dados brutos de 32 bits no formato TYPE2 do ESP32-S3:
 *   bits [11: 0] → value   (12 bits de resolução)
 *   bits [15:12] → channel (qual canal ADC originou a amostra)
 *   bit  [16]    → unit    (0 = ADC1, 1 = ADC2)
 *
 * Esta struct armazena apenas os campos relevantes após a decodificação.
 */
struct AdcDmaSample {
    uint8_t  channel; ///< Canal de origem (ex.: 3 → ADC1_CH3 → GPIO4)
    uint16_t value;   ///< Valor ADC 12 bits (0–4095 → 0 V–3.3 V com ATTEN_DB_12)
};

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @class AdcDmaEsp
 * @brief Wrapper para o ADC contínuo com DMA no ESP32-S3.
 *
 * Encapsula as três etapas do ESP-IDF:
 *   1. adc_continuous_new_handle()              → aloca pool DMA
 *   2. adc_continuous_config()                  → configura canais e frequência
 *   3. adc_continuous_register_event_callbacks() → callback de ISR por frame
 */
class AdcDmaEsp {
public:
    /** Tipo de callback chamado pelo update() quando há amostras prontas. */
    using SamplesReadyCb = void (*)();

    // ── Inicialização ────────────────────────────────────────────────────────

    /**
     * @brief Configura um único canal ADC1 com DMA.
     *
     * @param channel   Canal ADC1 (ex.: ADC_CHANNEL_3 → GPIO4 no ESP32-S3).
     * @param sampleHz  Frequência de amostragem total em Hz (611–83333 Hz no S3).
     * @return true se o driver foi inicializado corretamente.
     */
    bool begin(adc_channel_t channel, uint32_t sampleHz) {
        _channels[0]  = channel;
        _channelCount = 1;
        return _init(sampleHz);
    }

    /**
     * @brief Configura múltiplos canais ADC1 com DMA (varredura round-robin).
     *
     * O driver alterna entre os canais automaticamente; a frequência
     * por canal é sampleHz / channelCount.
     *
     * @param channels     Array de canais (ex.: {ADC_CHANNEL_3, ADC_CHANNEL_4}).
     * @param channelCount Tamanho do array (máx: ADC_DMA_MAX_CHANNELS).
     * @param sampleHz     Frequência de amostragem total em Hz.
     * @return true se o driver foi inicializado corretamente.
     */
    bool begin(const adc_channel_t* channels, uint8_t channelCount, uint32_t sampleHz) {
        if (!channels || channelCount == 0 || channelCount > ADC_DMA_MAX_CHANNELS)
            return false;
        for (uint8_t i = 0; i < channelCount; i++) _channels[i] = channels[i];
        _channelCount = channelCount;
        return _init(sampleHz);
    }

    // ── Controle ─────────────────────────────────────────────────────────────

    /**
     * @brief Inicia as conversões DMA.
     * Chamar após begin(). As amostras começam a chegar imediatamente.
     * @return true se o comando foi aceito pelo driver.
     */
    bool start() {
        if (!_handle) return false;
        return adc_continuous_start(_handle) == ESP_OK;
    }

    /**
     * @brief Para as conversões DMA sem liberar recursos.
     * Pode retomar chamando start() novamente.
     * @return true se o comando foi aceito pelo driver.
     */
    bool stop() {
        if (!_handle) return false;
        return adc_continuous_stop(_handle) == ESP_OK;
    }

    /**
     * @brief Libera o handle DMA e todos os recursos alocados.
     * Após deinit(), chame begin() para reusar o objeto.
     */
    void deinit() {
        if (_handle) {
            adc_continuous_deinit(_handle);
            _handle = nullptr;
        }
    }

    // ── Leitura ──────────────────────────────────────────────────────────────

    /**
     * @brief Retorna true quando a ISR sinalizou um frame completo.
     *
     * Use em loop() como condição para chamar read().
     * O flag é limpo automaticamente no início de read().
     */
    bool available() const { return _ready; }

    /**
     * @brief Lê e decodifica amostras do buffer DMA.
     *
     * Drena até ADC_DMA_FRAME_SIZE bytes do buffer circular e converte
     * cada palavra de 32 bits no formato TYPE2 para um AdcDmaSample.
     *
     * Decodificação do formato TYPE2 (32 bits por amostra):
     * @code
     *  31       17 16    13 12  11             0
     *  ┌─────────┬──────┬────┬──────────────────┐
     *  │ reserv. │ unit │ ch │      data        │
     *  └─────────┴──────┴────┴──────────────────┘
     * @endcode
     *
     * @param outBuf     Buffer de saída alocado pelo chamador.
     * @param maxSamples Capacidade de outBuf em número de AdcDmaSample.
     * @return Número de amostras efetivamente decodificadas (0 se nada).
     */
    size_t read(AdcDmaSample* outBuf, size_t maxSamples) {
        if (!_handle || !outBuf || maxSamples == 0) return 0;
        _ready = false;  // limpa o flag ANTES de ler para não perder o próximo frame

        // Drena um frame do buffer DMA. timeout=0 → retorna imediatamente.
        uint8_t  raw[ADC_DMA_FRAME_SIZE];
        uint32_t bytesRead = 0;
        if (adc_continuous_read(_handle, raw, sizeof(raw), &bytesRead, 0) != ESP_OK)
            return 0;

        // Cada amostra ocupa exatamente 4 bytes no formato TYPE2 do ESP32-S3
        const size_t total = bytesRead / 4;
        const size_t n     = (total < maxSamples) ? total : maxSamples;

        for (size_t i = 0; i < n; i++) {
            // Reinterpreta 4 bytes brutos como a union do ESP-IDF
            const auto* p = reinterpret_cast<const adc_digi_output_data_t*>(&raw[i * 4]);
            outBuf[i].channel = p->type2.channel;  // campo de 4 bits
            outBuf[i].value   = p->type2.data;      // campo de 12 bits (0–4095)
        }
        return n;
    }

    // ── Callbacks ────────────────────────────────────────────────────────────

    /**
     * @brief Registra função chamada por update() quando há amostras prontas.
     *
     * Diferente do callback de ISR interno, este é chamado fora de contexto
     * de interrupção — pode usar Serial, wserial, delay, etc.
     */
    void onSamplesReady(SamplesReadyCb cb) { _onReady = cb; }

    /**
     * @brief Verifica o flag e dispara onSamplesReady se houver dados.
     *
     * Alternativa ao poll manual de available(). Chame em loop().
     * Não misturar com o uso direto de available() + read() no mesmo loop.
     */
    void update() {
        if (_ready && _onReady) _onReady();
    }

    /** @brief Retorna o handle interno para uso avançado com o ESP-IDF. */
    adc_continuous_handle_t handle() const { return _handle; }

private:
    adc_continuous_handle_t  _handle              = nullptr;
    adc_channel_t            _channels[ADC_DMA_MAX_CHANNELS] = {};
    uint8_t                  _channelCount        = 0;
    volatile bool            _ready               = false;  // escrita na ISR, lida no loop
    SamplesReadyCb           _onReady             = nullptr;

    /**
     * @brief Inicializa o driver ADC contínuo do ESP-IDF em três etapas.
     *
     * Etapa 1 — new_handle: aloca o pool DMA e define o tamanho do frame.
     * Etapa 2 — config:     define canais, atenuação, frequência e formato.
     * Etapa 3 — callbacks:  registra a ISR que sinaliza frame completo.
     */
    bool _init(uint32_t sampleHz) {

        // ── Etapa 1: cria o handle e reserva o pool DMA ──────────────────────
        // max_store_buf_size: tamanho total do buffer circular (deve ser >= conv_frame_size)
        // conv_frame_size:    quantos bytes acumular antes de disparar a ISR
        adc_continuous_handle_cfg_t handleCfg = {};
        handleCfg.max_store_buf_size = ADC_DMA_POOL_SIZE;
        handleCfg.conv_frame_size    = ADC_DMA_FRAME_SIZE;

        if (adc_continuous_new_handle(&handleCfg, &_handle) != ESP_OK) {
            _handle = nullptr;
            return false;
        }

        // ── Etapa 2a: define o padrão de conversão por canal ─────────────────
        // atten=DB_12 → range de entrada 0–3.3 V (maior range, ~11 dB)
        // bit_width   → máxima resolução suportada pelo SoC (12 bits no S3)
        adc_digi_pattern_config_t pattern[ADC_DMA_MAX_CHANNELS] = {};
        for (uint8_t i = 0; i < _channelCount; i++) {
            pattern[i].atten     = ADC_ATTEN_DB_12;
            pattern[i].channel   = _channels[i] & 0x0F;
            pattern[i].unit      = ADC_UNIT_1;                 // somente ADC1 com DMA+WiFi
            pattern[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;  // 12 bits no ESP32-S3
        }

        // ── Etapa 2b: aplica configuração geral ──────────────────────────────
        // conv_mode=SINGLE_UNIT_1: apenas ADC1 (ADC2 não é confiável com WiFi ativo)
        // format=TYPE2:            saída inclui campos unit+channel+data (32 bits)
        // sample_freq_hz:          taxa total entre todos os canais
        adc_continuous_config_t cfg = {};
        cfg.pattern_num    = _channelCount;
        cfg.adc_pattern    = pattern;
        cfg.sample_freq_hz = sampleHz;
        cfg.conv_mode      = ADC_CONV_SINGLE_UNIT_1;
        cfg.format         = ADC_DIGI_OUTPUT_FORMAT_TYPE2;

        if (adc_continuous_config(_handle, &cfg) != ESP_OK) {
            deinit();
            return false;
        }

        // ── Etapa 3: registra ISR de "frame completo" ────────────────────────
        // user_data = this → a ISR recupera a instância para setar _ready
        adc_continuous_evt_cbs_t evtCbs = {};
        evtCbs.on_conv_done = _isrConvDone;

        if (adc_continuous_register_event_callbacks(_handle, &evtCbs, this) != ESP_OK) {
            deinit();
            return false;
        }

        return true;
    }

    /**
     * @brief ISR chamada pelo driver quando um frame DMA está completo.
     *
     * Roda em contexto de interrupção (IRAM) → código mínimo.
     * Apenas seta o flag _ready; a leitura acontece no loop() via read().
     *
     * @param user_data Ponteiro para a instância AdcDmaEsp (passado no register).
     * @return false → não força um task switch (equivale a pdFALSE).
     */
    static bool IRAM_ATTR _isrConvDone(
        adc_continuous_handle_t         /*handle*/,
        const adc_continuous_evt_data_t* /*edata*/,
        void*                            user_data)
    {
        static_cast<AdcDmaEsp*>(user_data)->_ready = true;
        return false;
    }
};

inline AdcDmaEsp adcDma; ///< Instância global do ADC com DMA.
