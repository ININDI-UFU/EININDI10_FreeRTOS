#ifndef __ADS1X15_H
#define __ADS1X15_H

/**
 * @file ADS1115_c.h
 * @brief Classe para simplificar o uso do ADS1115 com o Adafruit_ADS1X15.
 *
 * Esta classe herda a funcionalidade do Adafruit_ADS1115, fornecendo uma interface simplificada
 * para configuração e leitura de valores analógicos.
 */

#include <Adafruit_ADS1X15.h>
#include <Wire.h>

/**
 * @class ADS1115_c
 * @brief Classe para interação simplificada com o ADC ADS1115.
 *
 * Esta classe encapsula o funcionamento do ADS1115, definindo o ganho padrão
 * e oferecendo um método de leitura direta de canais analógicos.
 */
class ADS1115 : protected Adafruit_ADS1115 {
private:
    bool _started = false;
    uint8_t _address = 0x48;

    bool isI2CDevicePresent(TwoWire &wire, uint8_t address) {
        wire.beginTransmission(address);
        return wire.endTransmission() == 0;
    }

public:
    /**
     * @brief Construtor padrão.
     *
     * Inicializa a classe base Adafruit_ADS1115.
     */
    ADS1115() : Adafruit_ADS1115() {}

    /**
     * @brief Inicializa o dispositivo ADS1115.
     *
     * Define o ganho padrão como GAIN_TWOTHIRDS e inicializa o dispositivo.
     * @param wire Barramento configurado antes com Wire.begin(SDA, SCL).
     * @return true se o dispositivo foi inicializado com sucesso, false caso contrário.
     */
    bool begin(TwoWire &wire = Wire) {
        ((Adafruit_ADS1115 *)this)->setGain(adsGain_t::GAIN_TWOTHIRDS);
        ((Adafruit_ADS1115 *)this)->setDataRate(RATE_ADS1115_860SPS);

        _started = false;
        for (uint8_t address = 0x48; address <= 0x4B; ++address) {
            if (isI2CDevicePresent(wire, address) && ((Adafruit_ADS1115 *)this)->begin(address, &wire)) {
                _address = address;
                _started = true;
                break;
            }
        }
        return _started;
    }

    bool isStarted() const {
        return _started;
    }

    uint8_t address() const {
        return _address;
    }

    /**
     * @brief Lê o valor analógico de um canal especificado.
     *
     * @param channel O canal analógico a ser lido (0 a 3).
     * @return Valor analógico lido do canal (16 bits).
     */
    int16_t analogRead(uint8_t channel) {
        if (!_started || channel > 3) return 0;
        return ((Adafruit_ADS1115 *)this)->readADC_SingleEnded(channel);
    }

    int16_t analogReadPot1(void)
    {
        return analogRead(1);
    }

    int16_t analogReadPot2(void)
    {
        return analogRead(0);
    }

    int16_t analogRead4a20_1(void)
    {
        return analogRead(3);
    }

    int16_t analogRead4a20_2(void)
    {
        return analogRead(2);
    }    
};

inline ADS1115 ads1115; ///< Instância global do ADC ADS1115.
#endif 
