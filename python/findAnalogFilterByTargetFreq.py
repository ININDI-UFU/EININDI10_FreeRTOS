import numpy as np
import matplotlib.pyplot as plt


def _fator_butterworth(magnitude, ordem):
    return ((1 / (magnitude**2)) - 1)**(1 / (2 * ordem))


def _resolver_cortes_banda(omega0, bw, omega_alvo):
    """Inverte a transformacao passa-baixa -> banda (omega² ± omega_alvo*bw*omega - omega0² = 0)."""
    termo = np.sqrt((omega_alvo * bw)**2 + 4 * omega0**2)
    omega_inferior = (-omega_alvo * bw + termo) / 2
    omega_superior = (omega_alvo * bw + termo) / 2
    return omega_inferior, omega_superior


def findAnalogFilterByTargetFreq(fDesejada, ordem, filterType, desvio, isBP):
    filterType = filterType.lower()
    if filterType not in ('lowpass', 'highpass', 'bandpass', 'bandstop'):
        raise ValueError("filterType deve ser 'lowpass', 'highpass', 'bandpass' ou 'bandstop'.")
    if ordem <= 0:
        raise ValueError("ordem deve ser maior que zero.")
    if not (0 < desvio < 1):
        raise ValueError("desvio deve estar entre 0 e 1.")

    eh_banda = filterType in ('bandpass', 'bandstop')

    if eh_banda:
        if not isinstance(fDesejada, (list, tuple, np.ndarray)) or len(fDesejada) != 2:
            raise ValueError(
                f"filterType='{filterType}' precisa de uma banda (borda inferior e superior), "
                f"entao fDesejada deve ser uma lista [f1, f2]. Foi recebido fDesejada={fDesejada!r}."
            )
        f1, f2 = sorted(fDesejada)
        if f1 <= 0:
            raise ValueError("As frequencias de fDesejada devem ser maiores que zero.")
        omega1, omega2 = 2 * np.pi * f1, 2 * np.pi * f2
        omega0 = np.sqrt(omega1 * omega2)
        bw = omega2 - omega1
    else:
        if isinstance(fDesejada, (list, tuple, np.ndarray)):
            raise ValueError(
                f"filterType='{filterType}' tem um unico corte, entao fDesejada deve ser um numero, "
                f"nao uma lista de 2 frequencias. Foi recebido fDesejada={fDesejada!r}. "
                "Use filterType='bandpass' ou 'bandstop' se a intencao era definir uma banda [f1, f2]."
            )
        if fDesejada <= 0:
            raise ValueError("fDesejada deve ser maior que zero.")
        omega_d = 2 * np.pi * fDesejada

    # Magnitude alvo (na fronteira informada) e magnitude complementar (fronteira oposta)
    M_alvo = 1 - desvio if isBP else desvio
    M_comp = desvio if isBP else 1 - desvio
    A = _fator_butterworth(M_alvo, ordem)
    A_comp = _fator_butterworth(M_comp, ordem)

    if filterType == 'lowpass':
        omega_c = omega_d / A
        omega_comp = omega_c * A_comp
    elif filterType == 'highpass':
        omega_c = omega_d * A
        omega_comp = omega_c / A_comp
    elif filterType == 'bandpass':
        Omega_c = 1 / A
        Omega_comp = Omega_c * A_comp
        omega_c = _resolver_cortes_banda(omega0, bw, Omega_c)
        omega_comp = _resolver_cortes_banda(omega0, bw, Omega_comp)
    else:  # bandstop
        Omega_c = A
        Omega_comp = Omega_c / A_comp
        omega_c = _resolver_cortes_banda(omega0, bw, Omega_c)
        omega_comp = _resolver_cortes_banda(omega0, bw, Omega_comp)

    # Converter para Hz
    if eh_banda:
        fc_escolhido_hz = tuple(float(omega / (2 * np.pi)) for omega in omega_c)
        f_comp_hz = tuple(float(omega / (2 * np.pi)) for omega in omega_comp)
    else:
        fc_escolhido_hz = float(omega_c / (2 * np.pi))
        f_comp_hz = float(omega_comp / (2 * np.pi))

    # Frequências para o gráfico
    todas_freqs = list(fDesejada) if eh_banda else [fDesejada]
    todas_freqs += list(fc_escolhido_hz) if eh_banda else [fc_escolhido_hz]
    todas_freqs += list(f_comp_hz) if eh_banda else [f_comp_hz]
    f_max = max(todas_freqs) * 1.5
    freqs = np.linspace(0, f_max, 1000)
    w = 2 * np.pi * freqs

    with np.errstate(divide='ignore'):
        if filterType == 'lowpass':
            H = 1 / np.sqrt(1 + (w / omega_c)**(2 * ordem))
        elif filterType == 'highpass':
            H = 1 / np.sqrt(1 + (omega_c / w)**(2 * ordem))
        else:
            Omega = (omega0**2 - w**2) / (bw * w)
            if filterType == 'bandpass':
                H = 1 / np.sqrt(1 + (Omega / Omega_c)**(2 * ordem))
            else:
                H = 1 / np.sqrt(1 + (Omega_c / Omega)**(2 * ordem))

    print(("Frequência(s) Desejada(s) (Banda de Passagem)" if isBP else "Frequência(s) Desejada(s) (Banda de Rejeição)") + " [Amarelo]:", fDesejada)
    print("Frequência(s) de Corte [Verde]:", fc_escolhido_hz)
    print("Frequência(s) Complementar(es) (banda oposta) [Roxa]:", f_comp_hz)

    # Plotagem
    fig, ax = plt.subplots(figsize=(8, 5))
    ax.plot(freqs, H, label='Resposta do Filtro Butterworth', color='blue')
    for f in (list(fDesejada) if eh_banda else [fDesejada]):
        ax.axvline(f, color='yellow', linestyle='--', label='Frequência Desejada')
    for f in (fc_escolhido_hz if eh_banda else [fc_escolhido_hz]):
        ax.axvline(f, color='green', linestyle='--', label='Frequência de Corte')
    for f in (f_comp_hz if eh_banda else [f_comp_hz]):
        ax.axvline(f, color='purple', linestyle='--', label='Frequência Complementar')
    ax.scatter(list(fDesejada) if eh_banda else [fDesejada],
               [M_alvo] * (2 if eh_banda else 1),
               color='red', zorder=5, label=f'Magnitude alvo (desvio={desvio})')

    ax.set_xlim(0, f_max * 1.1)
    ax.set_ylim(0, 1.1)
    ax.set_xlabel("Frequência (Hz)")
    ax.set_ylabel("Magnitude")
    ax.set_title("Filtro Butterworth - Resposta em Frequência")
    ax.grid(True)
    handles, labels = ax.get_legend_handles_labels()
    by_label = dict(zip(labels, handles))
    ax.legend(by_label.values(), by_label.keys())
    plt.tight_layout()
    plt.show()

    return fc_escolhido_hz, f_comp_hz


if __name__ == '__main__':
    fc, _ = findAnalogFilterByTargetFreq(
        fDesejada=[400, 500],        # Frequência desejada em Hz (use [f1, f2] para bandpass/bandstop)
        ordem=2,
        filterType='bandstop',  # lowpass, highpass, bandpass ou bandstop
        desvio=0.05,
        isBP=False             # True = banda de passagem (fDesejada com magnitude >= 1-desvio)
    )
    print("Frequência de Corte escolhida (Hz):", fc)

    # Projeto do filtro Sallen-Key
    # R = 1000  # 1kΩ
    # c = 1 / (2 * np.pi * fc * R)
    # print("Valor da Capacitância (Farads):", c)
