# Projeto de Filtros Butterworth por Frequência Alvo

Esta pasta contém duas ferramentas para projetar filtros **Butterworth** a partir de
um critério muito específico (e muito útil na prática): *"eu quero que, numa certa
frequência, a magnitude do filtro seja exatamente um determinado valor"* — em vez do
critério mais comum de "me dê a frequência de corte de -3 dB".

| Arquivo | Domínio | Saída |
|---|---|---|
| [`findAnalogFilterByTargetFreq.py`](findAnalogFilterByTargetFreq.py) | Analógico (s, rad/s) | frequência(s) de corte em Hz, prontas para um circuito Sallen-Key/RC |
| [`findDigitalFilterIIRBiquadsByTargetFreq.py`](findDigitalFilterIIRBiquadsByTargetFreq.py) | Digital (z, amostras) | seções biquad (SOS) prontas para C/DSP embarcado |

Os dois suportam `filterType` igual a `'lowpass'`, `'highpass'`, `'bandpass'` ou
`'bandstop'`. Para `lowpass`/`highpass`, `fDesejada` é **um número** (Hz). Para
`bandpass`/`bandstop`, `fDesejada` é **uma lista `[f1, f2]`** (Hz) — a borda
inferior e a borda superior da banda.

Dependências: `numpy`, `scipy`, `matplotlib` (veja [`00_instalLibPython.bat`](00_instalLibPython.bat)).

---

## 1. Conceito central: "magnitude alvo numa frequência alvo"

Um filtro Butterworth de ordem `N` tem a famosa resposta "maximamente plana":

```
|H_LP(Ω)| = 1 / sqrt(1 + (Ω/Ωc)^(2N))
```

onde `Ω` é a frequência (rad/s, domínio analógico ou pré-distorcido) e `Ωc` é a
frequência de corte clássica, onde `|H(Ωc)| = 1/√2 ≈ 0.707` (-3 dB).

O problema de engenharia raramente é "eu quero corte em -3 dB". Na prática você
quer algo como: *"a banda passante deve manter pelo menos 95% de magnitude até
100 Hz, e a banda de rejeição deve cair para 5% a partir de tal frequência"*. Ou
seja, você conhece:

- **`fDesejada`** — a frequência onde você *sabe* qual magnitude quer.
- **`desvio`** — o quanto de magnitude você tolera perder/deixar passar (0 a 1).
- **`isBP`** (*is BandPass* / está na banda de passagem) — diz se `fDesejada` é um
  ponto da banda passante (magnitude alta, `1 - desvio`) ou da banda de rejeição
  (magnitude baixa, `desvio`).

Os dois scripts resolvem a mesma pergunta algébrica: **dado N e o ponto
`(fDesejada, magnitude_alvo)`, qual `Ωc` faz a curva de Butterworth passar
exatamente por esse ponto?**

Isolando `Ωc` da equação acima:

```
M_alvo = |H_LP(Ω_alvo)| = 1 / sqrt(1 + (Ω_alvo/Ωc)^(2N))

  =>  (Ω_alvo/Ωc)^(2N) = (1/M_alvo²) - 1
  =>  Ω_alvo/Ωc = A         onde  A = ((1/M_alvo²) - 1)^(1/(2N))
```

Esse fator `A` é calculado pela função auxiliar `_fator_butterworth(magnitude, ordem)`,
presente nos dois arquivos:

```python
def _fator_butterworth(magnitude, ordem):
    return ((1 / (magnitude**2)) - 1)**(1 / (2 * ordem))
```

A partir de `A`, a relação entre a frequência alvo e o corte depende do tipo de
filtro:

| `filterType` | Relação | Por quê |
|---|---|---|
| `lowpass` | `omega_c = omega_d / A` | banda passante é a região de `Ω` pequeno |
| `highpass` | `omega_c = omega_d * A` | banda passante é a região de `Ω` grande |
| `bandpass` | (ver §3) | comporta-se como lowpass no domínio do protótipo |
| `bandstop` | (ver §3) | comporta-se como highpass no domínio do protótipo |

Depois de achar `omega_c`, o mesmo truque é repetido **encadeado a partir de
`omega_c`** (não a partir de `omega_d` de novo!) para achar a frequência
**complementar** — o ponto na banda oposta onde a magnitude vale `M_comp`:

```
M_comp = desvio        se isBP=True   (fDesejada estava na passagem; o complementar está na rejeição)
M_comp = 1 - desvio    se isBP=False  (fDesejada estava na rejeição; o complementar está na passagem)

A_comp = ((1/M_comp²) - 1)^(1/(2N))

lowpass:  omega_comp = omega_c * A_comp
highpass: omega_comp = omega_c / A_comp
```

> ⚠️ **Por que "encadeado a partir de `omega_c`" importa tanto.** Calcular
> `omega_comp` direto de `omega_d` (em vez de a partir de `omega_c` já calculado)
> é um erro fácil de cometer — e foi exatamente um bug que apareceu durante o
> desenvolvimento deste código: a frequência complementar caía **dentro** da
> banda passante em vez de cair na rejeição. A relação correta é sempre uma
> cadeia: `fDesejada -> omega_c -> omega_comp`, nunca `fDesejada -> omega_comp`
> direto com o fator do outro lado.

### Exemplo numérico verificado (lowpass analógico)

```python
fc, fcomp = findAnalogFilterByTargetFreq(
    fDesejada=100, ordem=2, filterType='lowpass', desvio=0.05, isBP=True
)
# fc    = 174.425770 Hz   (onde a magnitude é exatamente 0.707, a -3 dB "oficial")
# fcomp = 779.567765 Hz   (onde a magnitude cai para 0.05, fundo da rejeição)
```

Confira: a 100 Hz a magnitude vale `1 - desvio = 0.95` (passagem); a 779,57 Hz vale
`desvio = 0.05` (rejeição). `fc` (174,43 Hz) é só um parâmetro intermediário — é
o valor de corte de -3 dB que, matematicamente, produz essa curva.

---

## 2. `findAnalogFilterByTargetFreq.py` — domínio analógico

```python
fc, f_comp = findAnalogFilterByTargetFreq(fDesejada, ordem, filterType, desvio, isBP)
```

| Parâmetro | Tipo | Significado |
|---|---|---|
| `fDesejada` | `float` ou `[f1, f2]` | frequência(s) alvo em Hz |
| `ordem` | `int` | ordem do filtro Butterworth (qualquer inteiro ≥ 1) |
| `filterType` | `str` | `'lowpass'`, `'highpass'`, `'bandpass'`, `'bandstop'` |
| `desvio` | `float` (0,1) | quanto de magnitude se tolera perder/deixar passar |
| `isBP` | `bool` | `True` se `fDesejada` está na banda passante |

Retorna `(fc, f_comp)` — escalares para lowpass/highpass, tuplas `(inferior, superior)`
para bandpass/bandstop. Sempre plota a resposta em frequência (`matplotlib`), com:

- linha azul: `|H(f)|`
- linhas amarelas tracejadas: `fDesejada`
- linhas verdes tracejadas: `fc` (corte)
- linhas roxas tracejadas: `f_comp` (complementar)
- pontos vermelhos: `(fDesejada, magnitude_alvo)` — confirmação visual de que o
  desvio pedido foi atingido exatamente nesse ponto

No final do arquivo, a Sallen-Key clássica de 1 polo usa `fc` direto:
`C = 1 / (2·π·fc·R)`, com `R` escolhido (ex. 1 kΩ).

---

## 3. Bandpass e bandstop: a transformação clássica passa-baixa → banda

Esta é a parte mais densa matematicamente. Quando `filterType` é `'bandpass'` ou
`'bandstop'`, `fDesejada = [f1, f2]` define uma **banda**, não um corte único.

### 3.1 A transformação

A transformação canônica que converte um protótipo passa-baixa em passa-banda é:

```
Ω(ω) = (ω0² - ω²) / (BW · ω)
```

onde `ω0` é a frequência central e `BW` a largura de banda (ambos em rad/s). O
código escolhe:

```python
omega0 = sqrt(omega1 * omega2)   # média geométrica
bw     = omega2 - omega1         # largura linear entre as duas bordas dadas
```

**Propriedade-chave** (e a razão de escolher `omega0` assim): com essa escolha,
`Ω(ω1) = +1` e `Ω(ω2) = -1`, **sempre**, para qualquer `f1, f2` — é uma
identidade algébrica, não depende da magnitude desejada. Verifique:

```
Ω(ω1) = (ω1·ω2 - ω1²) / (BW·ω1) = (ω2 - ω1)/BW = 1   (pois BW = ω2 - ω1)
Ω(ω2) = (ω1·ω2 - ω2²) / (BW·ω2) = (ω1 - ω2)/BW = -1
```

Ou seja: **as duas frequências que você passa em `fDesejada` mapeiam sempre para
o mesmo ponto `|Ω| = 1`** no protótipo passa-baixa. Isso é o que permite reusar
a mesma equação de `A` da seção 1 sem reinventar nada:

```
bandpass (como lowpass):  Ωc = 1 / A          Ω_comp = Ωc * A_comp
bandstop (como highpass): Ωc = A              Ω_comp = Ωc / A_comp
```

`A` usa `M_alvo` e `A_comp` usa `M_comp`, exatamente como antes — a única
diferença é que agora `Ωc` não é "a frequência de corte" diretamente; é um ponto
no domínio do protótipo que precisa ser **revertido** de volta para duas
frequências reais (a borda inferior e a superior da banda real do filtro).

### 3.2 Revertendo Ω para frequências reais

Dado um valor de `Ω` (por exemplo `Ωc` ou `Ω_comp`), quais frequências `ω` reais
produzem `Ω(ω) = +Ω` (lado inferior) e `Ω(ω) = -Ω` (lado superior)? Isolando `ω`
na definição de `Ω(ω)`:

```
Ω·BW·ω = ω0² - ω²
=>  ω² + Ω·BW·ω - ω0² = 0     (raiz dá o lado inferior, Ω(ω)=+Ω)
=>  ω² - Ω·BW·ω - ω0² = 0     (raiz dá o lado superior, Ω(ω)=-Ω)
```

Bhaskara nas duas, descartando a raiz negativa:

```
termo          = sqrt((Ω·BW)² + 4·ω0²)
ω_inferior     = (-Ω·BW + termo) / 2
ω_superior     = ( Ω·BW + termo) / 2
```

Isso é exatamente a função `_resolver_cortes_banda`:

```python
def _resolver_cortes_banda(omega0, bw, omega_alvo):
    termo = np.sqrt((omega_alvo * bw)**2 + 4 * omega0**2)
    omega_inferior = (-omega_alvo * bw + termo) / 2
    omega_superior = (omega_alvo * bw + termo) / 2
    return omega_inferior, omega_superior
```

**Propriedade de verificação rápida**: `ω_inferior · ω_superior = ω0²` sempre —
ou seja, a média *geométrica* das duas frequências resultantes é sempre `ω0`,
nunca a média aritmética. Isso explica uma observação comum ao olhar o gráfico:

> A curva de um bandpass/bandstop **não é simétrica numa escala linear de Hz**.
> Ela é simétrica numa escala **logarítmica** (geométrica). O lado de frequência
> mais baixa fica "mais comprimido" que o lado de frequência mais alta — isso é
> esperado e correto, não é bug.

### 3.3 Exemplo numérico verificado (bandpass analógico)

```python
fc, fcomp = findAnalogFilterByTargetFreq(
    fDesejada=[400, 500], ordem=4, filterType='bandpass', desvio=0.05, isBP=True
)
# fc    = (386.027481, 518.097830) Hz   -> magnitude 0.707 nas duas bordas
# fcomp = (328.893035, 608.100442) Hz   -> magnitude 0.05  nas duas bordas (rejeição)
```

Note a ordem física esperada (do mais afastado do centro para o mais próximo):

```
fcomp_inf (328.9) < fc_inf (386.0) < f1 (400) < f2 (500) < fc_sup (518.1) < fcomp_sup (608.1)
```

Ou seja: a complementar (rejeição, magnitude 0.05) sempre fica **fora** de `fc`
(magnitude 0.707), que por sua vez fica **fora** de `fDesejada` (magnitude 0.95,
dentro da banda passante). Se a complementar aparecer **dentro** dessa faixa, é
sinal de que o encadeamento `Ωc -> Ω_comp` foi quebrado (veja o aviso da seção 1
— esse exato bug já ocorreu na versão digital deste projeto, ver §5).

---

## 4. `findDigitalFilterIIRBiquadsByTargetFreq.py` — domínio digital (IIR/biquads)

```python
sos, fc, f_comp = findDigitalFilterIIRBiquadsByTargetFreq(
    fDesejada, ordem, fs, filterType="lowpass", desvio=0.05, isBP=True,
    plot=True, worN=4096,
)
```

O resumo no console (`_imprimir_resultado`) é sempre impresso; não há opção de
silenciá-lo.

| Parâmetro | Tipo | Significado |
|---|---|---|
| `fDesejada` | `float` ou `[f1, f2]` | igual ao analógico, mas em Hz **digitais** (< `fs/2`) |
| `ordem` | `int` | deve ser **par** e ≥ 2 (cada biquad = 2ª ordem) |
| `fs` | `float` | frequência de amostragem em Hz |
| `filterType`, `desvio`, `isBP` | — | mesmo significado da versão analógica |

Retorna `sos` — a matriz de seções de 2ª ordem no formato do `scipy`
(`[b0, b1, b2, a0, a1, a2]` por linha, com `a0 = 1`), além de `fc`/`f_comp` em Hz.

### 4.1 A diferença essencial: pré-distorção (*pre-warping*)

Tudo o que foi descrito nas seções 1–3 vale para o domínio **analógico contínuo**
(`s = jω`). Um filtro digital usa a **transformada bilinear**, que mapeia o eixo
`jω` analógico inteiro (de `-∞` a `+∞`) para um único período da frequência
digital (`-π` a `π`). Esse mapeamento é não-linear:

```
ω_analógico  =  (2/T) · tan(ω_digital · T / 2)      (T = 1/fs)
```

Por isso, antes de aplicar qualquer fórmula de Butterworth, o código converte
toda frequência em Hz digitais para o "espaço tan" com:

```python
tan_wd = np.tan(np.pi * fDesejada / fs)
```

e faz **toda a álgebra das seções 1–3 nesse espaço tan** (tratando `tan_wd` como
se fosse `omega_d` analógico) — porque a transformada bilinear preserva a forma
da equação de Butterworth quando expressa em termos de `tan(ω·T/2)`. Ao final,
desfaz a distorção com `arctan`:

```python
fc = np.arctan(tan_wc) * fs / np.pi
```

Esse é exatamente o mesmo truque usado por dentro do `scipy.signal.butter(N, Wn,
fs=fs)`, e é por isso que o `fc` calculado aqui pode ser passado direto como
`Wn` para o `scipy.signal.butter` — o resultado terá magnitude `1/√2` **exatamente**
em `fc`, e portanto a magnitude exigida (`1 - desvio` ou `desvio`) **exatamente**
em `fDesejada`.

### 4.2 Lowpass/highpass digital

```python
def _calcular_lowpass_highpass(fDesejada, ordem, fs, filterType, magnitude_desejada, magnitude_complementar):
    A      = _fator_butterworth(magnitude_desejada, ordem)
    A_comp = _fator_butterworth(magnitude_complementar, ordem)
    tan_wd = np.tan(np.pi * fDesejada / fs)

    if filterType == 'lowpass':
        tan_wc   = tan_wd / A
        tan_comp = tan_wc * A_comp     # <- encadeado a partir de tan_wc, não de tan_wd
    else:
        tan_wc   = tan_wd * A
        tan_comp = tan_wc / A_comp

    fc     = np.arctan(tan_wc)   * fs / np.pi
    f_comp = np.arctan(tan_comp) * fs / np.pi
    return fc, f_comp
```

Note o comentário "encadeado a partir de `tan_wc`" — é a mesma regra da seção 1,
só que aplicada no domínio tan em vez de `ω`.

### 4.3 Bandpass/bandstop digital

Idêntico à seção 3, mas com `tan1 = tan(π·f1/fs)`, `tan2 = tan(π·f2/fs)` no lugar
de `ω1, ω2`, e `tan0 = sqrt(tan1·tan2)`, `bw = tan2 - tan1` no lugar de `ω0, BW`:

```python
def _calcular_bandpass_bandstop(f1, f2, ordem, fs, filterType, magnitude_desejada, magnitude_complementar):
    tan1, tan2 = np.tan(np.pi * f1 / fs), np.tan(np.pi * f2 / fs)
    tan0 = np.sqrt(tan1 * tan2)
    bw   = tan2 - tan1

    A      = _fator_butterworth(magnitude_desejada, ordem)
    A_comp = _fator_butterworth(magnitude_complementar, ordem)

    if filterType == 'bandpass':
        Omega_c    = 1 / A
        Omega_comp = Omega_c * A_comp
    else:
        Omega_c    = A
        Omega_comp = Omega_c / A_comp

    tan_c_inf,    tan_c_sup    = _resolver_cortes_banda(tan0, bw, Omega_c)
    tan_comp_inf, tan_comp_sup = _resolver_cortes_banda(tan0, bw, Omega_comp)
    ...
```

### 4.4 Exemplo numérico verificado (lowpass digital)

```python
sos, fc, fcomp = findDigitalFilterIIRBiquadsByTargetFreq(
    fDesejada=100, ordem=4, fs=1600.0, filterType='lowpass', desvio=0.05, isBP=True,
    plot=False,
)
# fc    = 130.837795 Hz
# fcomp = 258.194067 Hz
# sos =
# [[ 0.002412  0.004825  0.002412  1.  -1.197851  0.375443]
#  [ 1.        2.        1.        1.  -1.46603   0.683382]]
```

Confirmado por `sosfreqz`: `|H(100 Hz)| = 0.95` e `|H(258.19 Hz)| = 0.05`, exatamente.

### 4.5 Exemplo numérico verificado (bandpass digital — igual ao `__main__` do arquivo)

```python
sos, fc, fcomp = findDigitalFilterIIRBiquadsByTargetFreq(
    fDesejada=[400, 500], ordem=10, fs=1600.0, filterType='bandpass', desvio=0.05, isBP=True,
)
# fc    = (394.053489, 505.469459) Hz
# fcomp = (374.611110, 523.020212) Hz
# sos.shape = (10, 6)  -> 10 biquads
```

> ⚠️ **Atenção à contagem de biquads em bandpass/bandstop.** Para `lowpass`/
> `highpass`, o número de biquads é `ordem / 2`. Mas o `scipy.signal.butter`
> **dobra** a ordem internamente para `bandpass`/`bandstop` (a transformação
> passa-baixa→banda duplica o número de polos), então `ordem=10` aqui produz
> **10 biquads**, não 5. O código não calcula isso manualmente — usa
> `sos.shape[0]` diretamente, então está sempre correto independente do tipo.

Ordem física: `fcomp_inf (374.6) < fc_inf (394.1) < 400 < 500 < fc_sup (505.5) < fcomp_sup (523.0)`
— mesma lógica da seção 3.3, agora no digital.

### 4.6 De `sos` para coeficientes em C (`formatBiquadsForC`)

O `scipy` devolve `sos` no formato `[b0, b1, b2, a0, a1, a2]` (com `a0 = 1`
sempre). Para **Direct Form II Transposto** (a forma usada em DSPs embarcados,
incluindo o `esp-dsp` do ESP32), o `a0` é descartado — ele já é implicitamente 1:

```python
def biquads_df2t(sos):
    """Coeficientes [b0, b1, b2, a1, a2] para Direct Form II Transposto."""
    return sos[:, [0, 1, 2, 4, 5]]
```

`formatBiquadsForC(sos)` imprime isso como um array C pronto para colar no
firmware:

```c
const float biquads_df2t[10][5] = {
    {7.179004984e-08f, -1.435800997e-07f, 7.179004984e-08f, 2.681785934e-01f, 6.381442659e-01f},
    {1.000000000e+00f, -2.000000000e+00f, 1.000000000e+00f, 3.836845981e-01f, 6.424150990e-01f},
    ...
};
```

Use `incluir_a0=True` se a sua função de DSP espera os 6 coeficientes
(`[b0, b1, b2, a0, a1, a2]`) em vez de 5.

---

## 5. Por que as seções biquad de uma cascata nunca podem ser idênticas

Esta pergunta surge na hora de implementar a cascata no `esp-dsp` (ESP32): será
que dois biquads consecutivos podem ter os mesmos coeficientes? **Não, e isso não
é uma regra do `esp-dsp` — é uma consequência da matemática de como um filtro de
ordem alta se decompõe em seções de 2ª ordem.**

### 5.1 De onde vêm os polos

Um filtro Butterworth de ordem `N` tem `N` polos no plano complexo, distribuídos
em ângulos **igualmente espaçados** ao longo de um semicírculo (analogicamente,
no plano-s; depois da transformada bilinear, no plano-z digital). A fórmula
clássica dos ângulos (protótipo analógico, referência usual em livros-texto) é:

```
θ_k = π/2 + (2k - 1)·π / (2N),   k = 1, ..., N/2
```

Cada seção biquad da decomposição SOS corresponde a **um par de polos complexos
conjugados**, ou seja, a **um único `θ_k`**. Como a fórmula gera um ângulo
**estritamente diferente** para cada `k` (é uma função injetora em `k`), dois
pares de polos nunca coincidem — logo, **nenhuma seção pode ter os mesmos
coeficientes `(a1, a2)` que outra**, para `N ≥ 4` (com `N = 2` só existe uma
seção, então a pergunta nem se aplica).

### 5.2 Verificação no código

Veja, no exemplo da seção 4.4 (`ordem=4`, lowpass digital), os polos de cada
seção (calculados com `numpy.roots` sobre o denominador `[a0, a1, a2]` de cada
linha de `sos`):

```
seção 1: polos = 0.5989 ± 0.1293j   ângulo ≈ 12.19°
seção 2: polos = 0.7330 ± 0.3822j   ângulo ≈ 27.54°
```

Dois ângulos diferentes ⇒ dois pares `(a1, a2)` diferentes ⇒ duas seções com
**Q (fator de qualidade/ressonância) diferentes**. Você pode reproduzir isso para
qualquer `sos` gerado pelos dois scripts: basta olhar a matriz impressa por
`_imprimir_sos`-style print ou rodar `numpy.roots([1, a1, a2])` linha a linha —
nunca duas linhas terão o mesmo par de raízes.

### 5.3 A implicação prática

A resposta "maximamente plana" do Butterworth **completo** (a cascata toda) só
existe porque cada seção, **individualmente**, tem um pico de ressonância (Q)
diferente — as seções com polos próximos do eixo real têm Q baixo (quase 1ª
ordem disfarçada de 2ª), as seções com polos próximos da "borda" têm Q mais alto
(uma leve ressonância antes do corte). É a soma dessas respostas desiguais, na
cascata, que se cancela e produz a curva suave característica do Butterworth.

Por isso, ao implementar a cascata no `esp-dsp` (ou em qualquer DSP), **é
esperado e correto** que cada biquad da estrutura `biquads_df2t[sos.shape[0]][5]`
tenha coeficientes diferentes dos outros. Se dois biquads saírem idênticos, o resultado
não seria mais um Butterworth válido — seria sinal de erro no cálculo dos polos
(ex.: ordem muito baixa sendo tratada como se fosse maior, ou uma decomposição
incorreta de `sos`), não uma cascata "redundante" que pudesse ser simplificada.

---

## 6. Mensagens de erro e validações

Os dois arquivos validam bastante para evitar o erro mais comum: misturar
`filterType` escalar (`lowpass`/`highpass`) com `fDesejada` em lista, ou
vice-versa.

```python
# filterType='bandpass' mas fDesejada é um número:
ValueError: filterType='bandpass' precisa de uma banda (borda inferior e
superior), entao fDesejada deve ser uma lista [f1, f2]. Foi recebido
fDesejada=100.

# filterType='lowpass' mas fDesejada é uma lista:
ValueError: filterType='lowpass' tem um unico corte, entao fDesejada deve ser
um numero, nao uma lista de 2 frequencias. Foi recebido fDesejada=[100, 200].
Use filterType='bandpass' ou 'bandstop' se a intencao era definir uma banda
[f1, f2].
```

Outras validações: `ordem` deve ser par e ≥ 2 no digital (cada biquad é de 2ª
ordem); `0 < desvio < 1`; `0 < fDesejada < fs/2` (Nyquist) no digital;
`f1, f2 > 0` no analógico.

---

## 7. Referência rápida de uso

```python
# Analógico, lowpass, Sallen-Key
fc, fcomp = findAnalogFilterByTargetFreq(
    fDesejada=100, ordem=2, filterType='lowpass', desvio=0.05, isBP=True,
)

# Analógico, bandpass
fc, fcomp = findAnalogFilterByTargetFreq(
    fDesejada=[400, 500], ordem=4, filterType='bandpass', desvio=0.05, isBP=True,
)

# Digital, lowpass, biquads para C
sos, fc, fcomp = findDigitalFilterIIRBiquadsByTargetFreq(
    fDesejada=100, ordem=10, fs=1600.0, filterType='lowpass', desvio=0.05, isBP=True,
)
print(formatBiquadsForC(sos))

# Digital, bandpass
sos, fc, fcomp = findDigitalFilterIIRBiquadsByTargetFreq(
    fDesejada=[400, 500], ordem=10, fs=1600.0, filterType='bandpass', desvio=0.05, isBP=True,
)
```
