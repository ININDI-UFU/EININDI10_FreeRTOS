#ifndef __DISPLAY_H
#define __DISPLAY_H

/**
 * @file Display_SSD1306.h
 * @brief Classe para manipulação de displays OLED utilizando a biblioteca Adafruit_SSD1306.
 *
 * Esta classe encapsula o funcionamento de displays OLED, fornecendo funcionalidades
 * para inicialização, atualização e configuração de texto com suporte a rolagem e modos de função.
 */

#include <Wire.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_ADDRESS 0x3C ///< Endereço I2C do display OLED.
#define SCREEN_WIDTH 128    ///< Largura do display em pixels.
#define SCREEN_HEIGHT 64    ///< Altura do display em pixels.
#define OLED_RESET -1       ///< Pino de reset (ou -1 para compartilhar com o reset do Arduino).

/**
 * @class Display_SSD1306
 * @brief Classe para gerenciamento de displays OLED.
 */
class Display_SSD1306 {
protected:
    Adafruit_SSD1306 *_display = nullptr;

    /**
     * @brief Realiza a rolagem de texto na linha especificada.
     * @param index Índice da linha a ser rolada.
     */
    void rotaty(uint8_t index);
    bool isFuncMode = false; ///< Indica se o display está no modo de função.
    bool isChanged = true; ///< Indica se houve alteração no conteúdo do display.
    bool scrollLeft[3] = {false, false, false}; ///< Flags de rolagem para cada linha.
    char ca_lineTxt[3][20] = {"Inicializando...", "", ""}; ///< Conteúdo das linhas do display.
    uint8_t ui8_lineSize[3] = {16, 0, 0}; ///< Tamanho do texto de cada linha.
    uint8_t ui8_txtSize[3] = {2, 2, 2}; ///< Tamanho da fonte para cada linha.
    int16_t i16_lineWidth[3] = {12, 12, 12}; ///< Largura inicial do texto em cada linha.
    int16_t i16_lineMinWidth[3]; ///< Largura mínima para rolagem do texto.

public:
    ~Display_SSD1306();

    /**
     * @brief Configura o texto a ser exibido em uma linha do display.
     * @param line Índice da linha (1 a 3).
     * @param txt Texto a ser exibido.
     * @param funcMode Modo de função (opcional).
     * @param txtSize Tamanho da fonte (opcional).
     */
    void setText(uint8_t line, const char txt[], bool funcMode = false, uint8_t txtSize = 2);

    /**
     * @brief Define o modo de função do display.
     * @param funcMode Modo de função (true ou false).
     */
    void setFuncMode(bool funcMode);

    /**
     * @brief Inicializa o display OLED usando um barramento I2C ja configurado.
     * @param wire Barramento configurado antes com Wire.begin(SDA, SCL).
     * @return true se a inicializacao foi bem-sucedida, false caso contrario.
     */
    bool begin(TwoWire &wire = Wire);

    /**
     * @brief Atualiza o conteúdo do display OLED.
     */
    void update(void);
};

bool Display_SSD1306::begin(TwoWire &wire) {
    delete _display;
    _display = new Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &wire, OLED_RESET);
    if (!_display) return false;

    // periphBegin=false: evita que SSD1306.begin() reinicialize o Wire e sobrescreva os pinos
    if (!_display->begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS, false, false)) {
        delete _display;
        _display = nullptr;
        return false;
    }
    setText(1, ca_lineTxt[0]);
    setText(2, ca_lineTxt[1]);
    setText(3, ca_lineTxt[2]);
    return true;
}

Display_SSD1306::~Display_SSD1306() {
    delete _display;
}

void Display_SSD1306::update(void) {
    if (!_display) return;

    const bool hasScrollingText = ui8_lineSize[0] > 10 || ui8_lineSize[1] > 10 || ui8_lineSize[2] > 10;
    static uint32_t lastScrollMs = 0;
    const uint32_t now = millis();

    if (!isChanged && (!hasScrollingText || now - lastScrollMs < 100)) {
        return;
    }

    if (hasScrollingText) {
        lastScrollMs = now;
    }

    isChanged = false;
    _display->clearDisplay();
    _display->setTextWrap(false);
    _display->setTextColor(SSD1306_WHITE);
    _display->cp437(true);
    rotaty(0);
    rotaty(1);
    rotaty(2);
    _display->display();
}

void Display_SSD1306::rotaty(uint8_t index) {
    if (!_display) return;

    if (ui8_lineSize[index] > 10) {
        _display->setTextSize(ui8_txtSize[index]);
        _display->setCursor(i16_lineWidth[index], index * 20);
        _display->print(ca_lineTxt[index]);
        if (scrollLeft[index]) {
            ++i16_lineWidth[index];
        } else {
            --i16_lineWidth[index];
        }
        if (i16_lineWidth[index] < i16_lineMinWidth[index]) {
            scrollLeft[index] = true;
        }
        if (i16_lineWidth[index] > 12) {
            scrollLeft[index] = false;
        }
    } else {
        _display->setTextSize(ui8_txtSize[index]);
        _display->setCursor(0, index * 20);
        _display->println(ca_lineTxt[index]);
    }
}

void Display_SSD1306::setText(uint8_t line, const char txt[], bool funcMode, uint8_t txtSize) {
    if (line < 1 || line > 3) {
        return;
    }
    if (this->isFuncMode == funcMode) {
        const uint8_t index = line - 1;
        strncpy(ca_lineTxt[index], txt ? txt : "", sizeof(ca_lineTxt[index]) - 1);
        ca_lineTxt[index][sizeof(ca_lineTxt[index]) - 1] = '\0';
        ui8_lineSize[index] = strlen(ca_lineTxt[index]);
        i16_lineMinWidth[index] = -12 * (ui8_lineSize[index] - 9);
        ui8_txtSize[index] = txtSize;
        isChanged = true;
    }
    update();
}

void Display_SSD1306::setFuncMode(bool funcMode) {
    this->isFuncMode = funcMode;
}

inline Display_SSD1306 disp;    ///< Display OLED.
#endif 
