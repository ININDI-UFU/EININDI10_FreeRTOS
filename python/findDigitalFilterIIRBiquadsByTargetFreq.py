import numpy as np
from scipy.signal import butter, sosfreqz


def _fator_butterworth(magnitude, ordem):
    if not (0 < magnitude < 1):
        raise ValueError("A magnitude deve estar entre 0 e 1.")
    return ((1 / (magnitude**2)) - 1)**(1 / (2 * ordem))


def _resolver_cortes_banda(tan0, bw, omega_alvo):
    """Inverte a transformacao passa-baixa -> banda no dominio pre-distorcido (tan)."""
    termo = np.sqrt((omega_alvo * bw)**2 + 4 * tan0**2)
    tan_inferior = (-omega_alvo * bw + termo) / 2
    tan_superior = (omega_alvo * bw + termo) / 2
    return tan_inferior, tan_superior


def _calcular_lowpass_highpass(fDesejada, ordem, fs, filterType, magnitude_desejada, magnitude_complementar):
    """fc vem de fDesejada; f_complementar encadeia a partir de fc (nao de fDesejada de novo)."""
    A = _fator_butterworth(magnitude_desejada, ordem)
    A_comp = _fator_butterworth(magnitude_complementar, ordem)
    tan_wd = np.tan(np.pi * fDesejada / fs)

    if filterType == 'lowpass':
        tan_wc = tan_wd / A
        tan_comp = tan_wc * A_comp
    else:
        tan_wc = tan_wd * A
        tan_comp = tan_wc / A_comp

    fc = float(np.arctan(tan_wc) * fs / np.pi)
    f_comp = float(np.arctan(tan_comp) * fs / np.pi)
    return fc, f_comp


def _calcular_bandpass_bandstop(f1, f2, ordem, fs, filterType, magnitude_desejada, magnitude_complementar):
    """Omega_c vem de f1/f2; Omega_comp encadeia a partir de Omega_c (nao de f1/f2 de novo)."""
    tan1, tan2 = np.tan(np.pi * f1 / fs), np.tan(np.pi * f2 / fs)
    tan0 = np.sqrt(tan1 * tan2)
    bw = tan2 - tan1

    A = _fator_butterworth(magnitude_desejada, ordem)
    A_comp = _fator_butterworth(magnitude_complementar, ordem)

    if filterType == 'bandpass':
        Omega_c = 1 / A
        Omega_comp = Omega_c * A_comp
    else:
        Omega_c = A
        Omega_comp = Omega_c / A_comp

    tan_c_inf, tan_c_sup = _resolver_cortes_banda(tan0, bw, Omega_c)
    tan_comp_inf, tan_comp_sup = _resolver_cortes_banda(tan0, bw, Omega_comp)

    fc = (float(np.arctan(tan_c_inf) * fs / np.pi), float(np.arctan(tan_c_sup) * fs / np.pi))
    f_comp = (float(np.arctan(tan_comp_inf) * fs / np.pi), float(np.arctan(tan_comp_sup) * fs / np.pi))
    return fc, f_comp


def findDigitalFilterIIRBiquadsByTargetFreq(
    fDesejada,
    ordem,
    fs,
    filterType="lowpass",
    desvio=0.05,
    isBP=True,
    plot=True,
    worN=4096,
):
    """
    Calcula um filtro IIR Butterworth e retorna suas secoes biquad (sos).

    filterType: 'lowpass', 'highpass', 'bandpass' ou 'bandstop'.
    Para 'bandpass'/'bandstop', fDesejada deve ser [f1, f2].

    isBP=True significa que fDesejada esta na banda de passagem.
    isBP=False significa que fDesejada esta na banda de rejeicao.
    """
    filterType = str(filterType).lower()
    if filterType not in ("lowpass", "highpass", "bandpass", "bandstop"):
        raise ValueError("filterType deve ser 'lowpass', 'highpass', 'bandpass' ou 'bandstop'.")
    if fs <= 0:
        raise ValueError("fs deve ser maior que zero.")
    if ordem < 2 or ordem % 2 != 0:
        raise ValueError("ordem deve ser par e maior ou igual a 2.")
    if not (0 < desvio < 1):
        raise ValueError("desvio deve estar entre 0 e 1.")

    eh_banda = filterType in ("bandpass", "bandstop")
    nyq = fs / 2

    if eh_banda:
        if not isinstance(fDesejada, (list, tuple, np.ndarray)) or len(fDesejada) != 2:
            raise ValueError(
                f"filterType='{filterType}' precisa de uma banda (borda inferior e superior), "
                f"entao fDesejada deve ser uma lista [f1, f2]. Foi recebido fDesejada={fDesejada!r}."
            )
        f1, f2 = sorted(float(f) for f in fDesejada)
        if not (0 < f1 < nyq and 0 < f2 < nyq):
            raise ValueError(f"As frequencias de fDesejada devem estar entre 0 e fs/2 ({nyq} Hz).")
    else:
        if isinstance(fDesejada, (list, tuple, np.ndarray)):
            raise ValueError(
                f"filterType='{filterType}' tem um unico corte, entao fDesejada deve ser um numero, "
                f"nao uma lista de 2 frequencias. Foi recebido fDesejada={fDesejada!r}. "
                "Use filterType='bandpass' ou 'bandstop' se a intencao era definir uma banda [f1, f2]."
            )
        fDesejada = float(fDesejada)
        if not (0 < fDesejada < nyq):
            raise ValueError(f"fDesejada={fDesejada} Hz deve estar entre 0 e fs/2 ({nyq} Hz).")

    magnitude_desejada = 1 - desvio if isBP else desvio
    magnitude_complementar = desvio if isBP else 1 - desvio

    if eh_banda:
        fc, f_complementar_hz = _calcular_bandpass_bandstop(
            f1, f2, ordem, fs, filterType, magnitude_desejada, magnitude_complementar
        )
    else:
        fc, f_complementar_hz = _calcular_lowpass_highpass(
            fDesejada, ordem, fs, filterType, magnitude_desejada, magnitude_complementar
        )

    sos = butter(ordem, fc, btype=filterType, fs=fs, output="sos")

    pontos_alvo = [f1, f2] if eh_banda else [fDesejada]
    w_alvo = [2 * np.pi * f / fs for f in pontos_alvo]
    _, h_alvo = sosfreqz(sos, worN=w_alvo)
    magnitude_obtida = float(np.mean(np.abs(h_alvo)))

    _imprimir_resultado(fDesejada, ordem, fs, filterType, fc, f_complementar_hz, magnitude_desejada, magnitude_obtida)

    if plot:
        _plotar_resultado(fDesejada, fs, filterType, fc, f_complementar_hz, magnitude_desejada, desvio, sos, worN)

    return sos, fc, f_complementar_hz


def biquads_df2t(sos):
    """Coeficientes [b0, b1, b2, a1, a2] para Direct Form II Transposto."""
    return sos[:, [0, 1, 2, 4, 5]]


def formatBiquadsForC(sos, nome_array="biquads_df2t", incluir_a0=False, casas=9):
    """
    Formata os biquads em um array C.

    Por padrao usa [b0, b1, b2, a1, a2], ideal para Direct Form II Transposto.
    Use incluir_a0=True para gerar [b0, b1, b2, a0, a1, a2].
    """
    matriz = sos if incluir_a0 else biquads_df2t(sos)
    linhas = [f"const float {nome_array}[{matriz.shape[0]}][{matriz.shape[1]}] = {{"]

    for secao in matriz:
        valores = ", ".join(f"{valor:.{casas}e}f" for valor in secao)
        linhas.append(f"    {{{valores}}},")

    linhas.append("};")
    return "\n".join(linhas)


def formatBiquadsAsStaticFloats(sos, prefixo="lowpass_stage", sufixo="_coeffs", incluir_a0=False, incluir_state=True):
    """
    Formata os biquads como arrays 'static float' separados por estagio,
    no mesmo estilo usado em main2.cpp (pronto para copiar e colar):

        static float lowpass_stage1_coeffs[5] = {
            ...
        };

    Se incluir_state=True, tambem gera os arrays de estado (zerados)
    exigidos por dsps_biquad_f32.
    """
    matriz = sos if incluir_a0 else biquads_df2t(sos)
    blocos = []

    for i, secao in enumerate(matriz, start=1):
        nome = f"{prefixo}{i}{sufixo}"
        linhas = [f"static float {nome}[{secao.shape[0]}] = {{"]
        linhas += [f"    {repr(float(valor))}f," for valor in secao]
        linhas.append("};")
        blocos.append("\n".join(linhas))

    if incluir_state:
        for i in range(1, matriz.shape[0] + 1):
            blocos.append(f"static float {prefixo}{i}_state[2] = {{0.0f, 0.0f}};")

    return "\n\n".join(blocos)


def _imprimir_resultado(fDesejada, ordem, fs, filterType, fc, f_complementar_hz, magnitude_desejada, magnitude_obtida):
    print("Resultado do filtro IIR em biquads:")
    print(f"  Tipo: {filterType}")
    print(f"  Ordem: {ordem}")
    print(f"  Frequencia(s) desejada(s): {fDesejada} Hz")
    print(f"  Frequencia(s) de corte: {fc}")
    print(f"  Frequencia(s) complementar(es): {f_complementar_hz}")
    print(f"  Magnitude alvo/obtida: {magnitude_desejada:.9f} / {magnitude_obtida:.9f}")


def _plotar_resultado(fDesejada, fs, filterType, fc, f_complementar_hz, magnitude_desejada, desvio, sos, worN):
    from matplotlib.pyplot import show, subplots

    eh_banda = filterType in ("bandpass", "bandstop")
    w, h = sosfreqz(sos, worN=worN)
    freqs_hz = w * fs / (2 * np.pi)
    h_mag = np.abs(h)

    fig, ax = subplots(figsize=(14, 5))
    ax.plot(freqs_hz, h_mag, label="Resposta do filtro IIR em biquads", color="blue")
    for f in (list(fDesejada) if eh_banda else [fDesejada]):
        ax.axvline(f, color="yellow", linestyle="--", label="Frequencia desejada")
    for f in (list(fc) if eh_banda else [fc]):
        ax.axvline(f, color="green", linestyle="--", label="Frequencia de corte")
    for f in (list(f_complementar_hz) if eh_banda else [f_complementar_hz]):
        ax.axvline(f, color="purple", linestyle="--", label="Frequencia complementar")
    ax.scatter(list(fDesejada) if eh_banda else [fDesejada],
               [magnitude_desejada] * (2 if eh_banda else 1),
               color="red", zorder=5, label=f"Magnitude alvo (desvio={desvio})")

    todas_freqs = (list(fDesejada) if eh_banda else [fDesejada]) + list(fc if eh_banda else [fc]) + list(f_complementar_hz if eh_banda else [f_complementar_hz])
    f_min_plot = max(0, min(todas_freqs) * 0.8)
    f_max_plot = min(fs / 2, max(todas_freqs) * 1.5)
    ax.set_xlim(f_min_plot, f_max_plot)
    ax.set_ylim(0, 1.1)
    ax.set_xlabel("Frequencia (Hz)")
    ax.set_ylabel("Magnitude")
    ax.set_title(f"Filtro IIR Butterworth - {sos.shape[0]} biquad(s)")
    ax.grid(True)
    handles, labels = ax.get_legend_handles_labels()
    by_label = dict(zip(labels, handles))
    ax.legend(by_label.values(), by_label.keys())
    show()


if __name__ == '__main__':
    sos, fc, f_comp = findDigitalFilterIIRBiquadsByTargetFreq(
        fDesejada=[400, 500],  # Frequência desejada em Hz (use [f1, f2] para bandpass/bandstop)
        ordem=10,
        fs=1600.0,
        filterType="bandpass",  # lowpass, highpass, bandpass ou bandstop
        desvio=0.05,
        isBP=True,
        plot=True,
    )
    print("Frequência(s) de Corte escolhida(s) (Hz):", fc)
    print("\nCoeficientes para Direct Form II Transposto:")
    print(formatBiquadsForC(sos))
    print("\nPronto para colar no ESP32 (static float por estagio):")
    print(formatBiquadsAsStaticFloats(sos, prefixo="lowpass_stage"))
