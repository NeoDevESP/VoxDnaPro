#pragma once
#include <algorithm>
#include <array>
#include <cmath>

// ============================================================================
//  DNA ANALOG v2.3 — modelos de circuito (código propio)
//
//   · DiodeClipperWDF : fuente resistiva + condensador + par de diodos
//                       (asimétricos) como raíz de un árbol WDF, resuelto en
//                       forma cerrada con la función omega de Wright
//                       (Werner, Nangia, Smith, Abel — DAFx 2015).
//   · TriodeStage     : etapa 12AX7 en cátodo común (modelo de Koren),
//                       recta de carga resuelta con Newton-Raphson, conducción
//                       de rejilla. Con el cátodo desacoplado la etapa no tiene
//                       memoria: la transferencia se resuelve una vez en
//                       prepare() y se guarda en una tabla (4096 puntos, cúbica).
//   · TapeHysteresis  : histéresis magnética de Jiles-Atherton, integrada con
//                       Runge-Kutta 2 (formulación de Chowdhury, DAFx 2019).
//
//  Todos trabajan en unidades físicas internas; la entrada se escala con el
//  "drive" y la salida se normaliza por la ganancia de pequeña señal medida en
//  prepare(), de modo que con señales débiles la ganancia es 0 dB (A/B
//  nivelado) y la distorsión crece con el nivel como en el circuito real.
//  Ninguno asigna memoria al procesar.
// ============================================================================
namespace pv::circuits
{
// ---------------------------------------------------------------------------
// Omega de Wright: w + ln(w) = x. Aproximación de D'Angelo et al. + Newton.
// ---------------------------------------------------------------------------
inline double wrightOmega (double x) noexcept
{
    double w;
    if (x < -3.341459552768620)
        w = std::exp (x);
    else if (x < 8.0)
        w = 6.313183464296682e-1 + x * (3.631952663804445e-1 + x * (4.775931364975583e-2 + x * -1.314293149877800e-3));
    else
        w = x - std::log (x);
    w = std::max (w, 1.0e-300);
    for (int i = 0; i < 3; ++i)   // Newton sobre f(w) = w + ln w - x
        w = std::max (1.0e-300, w - (w + std::log (w) - x) / (1.0 + 1.0 / w));
    return w;
}

// ---------------------------------------------------------------------------
// Clipper de diodos en WDF:
//      Vin --[Rs]--+-- salida
//                  |        |
//                 [C]   diodos antiparalelo (raíz)
//                  |        |
//                 GND      GND
//  Árbol: adaptador paralelo (fuente resistiva, C) -> raíz no lineal.
// ---------------------------------------------------------------------------
class DiodeClipperWDF
{
public:
    struct Params
    {
        double rs = 2200.0;        // ohmios
        double c = 3.3e-9;         // faradios
        double isPos = 2.52e-9;    // corriente de saturación del diodo que conduce en + (A)
        double isNeg = 2.52e-9;
        double nVtPos = 1.752 * 25.85e-3;   // n * Vt
        double nVtNeg = 1.752 * 25.85e-3;
    };

    void prepare (double sampleRate, const Params& p)
    {
        prm = p;
        const double T = 1.0 / sampleRate;
        rc = T / (2.0 * prm.c);                  // condensador bilineal
        gS = 1.0 / prm.rs; gC = 1.0 / rc;
        rRoot = 1.0 / (gS + gC);                 // puerto adaptado hacia la raíz
        gammaS = gS / (gS + gC);
        gammaC = gC / (gS + gC);
        logRisPos = std::log (rRoot * prm.isPos / prm.nVtPos);
        logRisNeg = std::log (rRoot * prm.isNeg / prm.nVtNeg);
        reset();
    }

    void reset() noexcept { capState = 0.0; }

    // ganancia en DC con señal débil: divisor Rs / resistencia dinámica del diodo (nVt/Is)
    double getSmallSignalGain() const noexcept
    {
        const double gd = 0.5 * (prm.isPos / prm.nVtPos + prm.isNeg / prm.nVtNeg);
        return 1.0 / (1.0 + prm.rs * gd);
    }

    // entrada y salida en voltios
    double process (double vin) noexcept
    {
        const double bS = vin;              // fuente resistiva: b = Vs
        const double bC = capState;         // condensador: b[n] = a[n-1]
        const double a = gammaS * bS + gammaC * bC;   // onda hacia la raíz

        // raíz: par de diodos (aproximación de Werner, el diodo inverso se desprecia)
        const bool pos = a >= 0.0;
        const double is = pos ? prm.isPos : prm.isNeg;
        const double nvt = pos ? prm.nVtPos : prm.nVtNeg;
        const double lam = pos ? 1.0 : -1.0;
        const double ris = rRoot * is;
        const double w = wrightOmega ((pos ? logRisPos : logRisNeg) + lam * a / nvt + ris / nvt);
        const double b = a + 2.0 * lam * (ris - nvt * w);

        const double v = 0.5 * (a + b);     // tensión en el nudo
        capState = 2.0 * v - bC;            // onda incidente al condensador (se refleja en n+1)
        return v;
    }

private:
    Params prm;
    double rc = 1.0, gS = 1.0, gC = 1.0, rRoot = 1.0, gammaS = 0.5, gammaC = 0.5;
    double logRisPos = 0.0, logRisNeg = 0.0;
    double capState = 0.0;
};

// ---------------------------------------------------------------------------
// Triodo 12AX7 en cátodo común (modelo de Koren), cátodo desacoplado.
//   Ip = (E1^Ex / Kg1) * (1 + sgn E1),  E1 = Vpk/Kp * ln(1 + exp(Kp (1/mu + Vgk / sqrt(Kvb + Vpk²))))
//   Vp = B+ - Rp * Ip   (resuelto por Newton-Raphson desde la solución anterior)
// ---------------------------------------------------------------------------
class TriodeStage
{
public:
    static constexpr double tableMin = -12.0, tableMax = 6.0;   // excursión de rejilla (V)
    static constexpr int tableSize = 4096;

    void prepare()
    {
        vpQ = quiescent (0.0);
        const double d = 1.0e-3;
        smallSignalGain = (quiescent (d) - quiescent (-d)) / (2.0 * d);   // negativa (inversora)
        double v = bPlus * 0.5;
        for (int i = 0; i < tableSize; ++i)
        {
            const double vg = tableMin + (tableMax - tableMin) * i / (tableSize - 1);
            double vgk = vg - vBias;
            if (vgk > 0.0) vgk = vgk / (1.0 + 1.2 * vgk);   // conducción de rejilla
            v = solve (vgk, v);
            table[(size_t) i] = v - vpQ;
        }
    }

    void reset() noexcept {}

    // vgIn: excursión de rejilla en voltios (se suma a la polarización). Devuelve Vp - Vp(reposo).
    double process (double vgIn) const noexcept
    {
        const double pos = (std::clamp (vgIn, tableMin, tableMax) - tableMin) / (tableMax - tableMin) * (tableSize - 1);
        const int i = std::clamp ((int) pos, 1, tableSize - 3);
        const double t = pos - i;
        const double y0 = table[(size_t) i - 1], y1 = table[(size_t) i], y2 = table[(size_t) i + 1], y3 = table[(size_t) i + 2];
        return y1 + 0.5 * t * (y2 - y0 + t * (2.0 * y0 - 5.0 * y1 + 4.0 * y2 - y3 + t * (3.0 * (y1 - y2) + y3 - y0)));
    }

    double getSmallSignalGain() const noexcept { return smallSignalGain; }

private:
    static constexpr double mu = 100.0, ex = 1.4, kg1 = 1060.0, kp = 600.0, kvb = 300.0;
    static constexpr double bPlus = 250.0, rp = 100000.0, vBias = 1.5;

    static double plateCurrent (double vgk, double vpk) noexcept
    {
        vpk = std::max (vpk, 1.0e-3);
        const double arg = kp * (1.0 / mu + vgk / std::sqrt (kvb + vpk * vpk));
        const double soft = arg > 30.0 ? arg : std::log1p (std::exp (arg));
        const double e1 = vpk / kp * soft;
        return e1 > 0.0 ? 2.0 * std::pow (e1, ex) / kg1 : 0.0;   // amperios
    }

    static double solve (double vgk, double guess) noexcept
    {
        double v = std::clamp (guess, 1.0, bPlus);
        for (int it = 0; it < 6; ++it)
        {
            const double f = v - bPlus + rp * plateCurrent (vgk, v);
            const double h = 1.0e-3;
            const double df = 1.0 + rp * (plateCurrent (vgk, v + h) - plateCurrent (vgk, v - h)) / (2.0 * h);
            const double step = f / df;
            v = std::clamp (v - step, 0.5, bPlus);
            if (std::abs (step) < 1.0e-6) break;
        }
        return v;
    }

    double quiescent (double vgIn) const noexcept
    {
        double v = bPlus * 0.5;
        for (int k = 0; k < 4; ++k) v = solve (vgIn - vBias, v);
        return v;
    }

    double vpQ = 125.0, smallSignalGain = -60.0;
    std::array<double, tableSize> table {};
};

// ---------------------------------------------------------------------------
// Histéresis de Jiles-Atherton (cinta magnética), RK2.
//   dM/dH = [ (1-c) dM_irr + c (Ms/a) L'(Q) ] / [ 1 - c α (Ms/a) L'(Q) ]
//   Q = (H + αM)/a, L = Langevin, dM_irr = δM (Man - M) / ((1-c) δ k - α (Man - M))
// ---------------------------------------------------------------------------
class TapeHysteresis
{
public:
    void prepare()
    {
        reset();
        // susceptibilidad de pequeña señal: respuesta a una senoide muy débil
        double num = 0.0, den = 0.0;
        const double hs = 30.0;
        for (int i = 0; i < 4000; ++i)
        {
            const double h = hs * std::sin (6.283185307179586 * (double) i / 200.0);
            const double mOut = process (h);
            if (i >= 2000) { num += mOut * h; den += h * h; }
        }
        chi0 = num / den;
        reset();
    }

    void reset() noexcept { m = 0.0; hPrev = 0.0; }

    double process (double h) noexcept
    {
        const double dh = h - hPrev;
        if (std::abs (dh) > 1.0e-12)
        {
            const double k1 = dMdH (hPrev, m, dh) * dh;
            const double k2 = dMdH (hPrev + 0.5 * dh, m + 0.5 * k1, dh) * dh;
            m = std::clamp (m + k2, -ms, ms);
        }
        hPrev = h;
        return m;
    }

    double getSmallSignalSusceptibility() const noexcept { return chi0; }

private:
    static constexpr double ms = 3.5e5, a = 2.2e4, alpha = 1.6e-3, k = 2.7e4, c = 1.7e-1;

    static double langevin (double q) noexcept
    {
        if (std::abs (q) < 1.0e-3) return q / 3.0 - q * q * q / 45.0;
        return 1.0 / std::tanh (q) - 1.0 / q;
    }
    static double langevinDeriv (double q) noexcept
    {
        if (std::abs (q) < 1.0e-3) return 1.0 / 3.0 - q * q / 15.0;
        const double s = std::sinh (q);
        return 1.0 / (q * q) - 1.0 / (s * s);
    }

    static double dMdH (double h, double mm, double dh) noexcept
    {
        const double q = (h + alpha * mm) / a;
        const double man = ms * langevin (q);
        const double dManDQ = (ms / a) * langevinDeriv (q);
        const double delta = dh > 0.0 ? 1.0 : -1.0;
        const double diff = man - mm;
        const double deltaM = delta * diff > 0.0 ? 1.0 : 0.0;
        double denom = (1.0 - c) * delta * k - alpha * diff;
        if (std::abs (denom) < 1.0e-6) denom = denom < 0.0 ? -1.0e-6 : 1.0e-6;
        const double irr = (1.0 - c) * deltaM * diff / denom;
        double d2 = 1.0 - c * alpha * dManDQ;
        if (std::abs (d2) < 1.0e-6) d2 = 1.0e-6;
        return (irr + c * dManDQ) / d2;
    }

    double m = 0.0, hPrev = 0.0, chi0 = 1.0;
};
} // namespace pv::circuits
