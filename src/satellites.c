#include <linux/string.h>
#include <linux/kernel.h>
#include "satellites.h"

/* Все вычисления в целых числах, масштаб x1000 */
#define MARS_SOLAR_FACTOR 433  /* 0.433 * 1000 */

const SatelliteModel SATELLITE_MODELS[MAX_MODELS] = {
    {
        .model_name       = "ARES-1",
        .parent_body      = "Mars",
        .parent_body_type = "planet",
        .orbit_type       = "Low Mars Orbit (LMO, ~400 km)",
        .operator         = "IMSA (Interplanetary Mars Survey Agency)",
        .launch_year      = 2041,
        .battery_capacity = 80000,
        .solar_max_power  = 120000,
        .temp_min         = -120000,
        .temp_max         =  60000,
        .tx_power_max     =  15000
    },
    {
        .model_name       = "PHOBOS-2",
        .parent_body      = "Mars",
        .parent_body_type = "planet",
        .orbit_type       = "Elliptical Mars Orbit (200x8000 km)",
        .operator         = "MRO Consortium",
        .launch_year      = 2044,
        .battery_capacity = 200000,
        .solar_max_power  = 250000,
        .temp_min         = -140000,
        .temp_max         =   80000,
        .tx_power_max     =   40000
    },
    {
        .model_name       = "DEIMOS-3",
        .parent_body      = "Mars",
        .parent_body_type = "planet",
        .orbit_type       = "Areostationary Orbit (17032 km)",
        .operator         = "DeepSpace Relay Inc.",
        .launch_year      = 2047,
        .battery_capacity = 500000,
        .solar_max_power  = 600000,
        .temp_min         = -100000,
        .temp_max         =   90000,
        .tx_power_max     =  100000
    }
};

const SatelliteModel *find_model(const char *name)
{
    int i;
    for (i = 0; i < MAX_MODELS; i++) {
        if (strcmp(SATELLITE_MODELS[i].model_name, name) == 0)
            return &SATELLITE_MODELS[i];
    }
    return NULL;
}

/*
 * cos_approx: приближение cos(angle) * 1000
 * angle в градусах 0..360
 * Используем таблицу значений cos для кратных 15 градусов
 */
static long cos_approx(long angle_deg)
{
    /* cos * 1000 для 0,15,30,45,60,75,90 градусов */
    static const long cos_table[7] = {
        1000, 966, 866, 707, 500, 259, 0
    };
    long a = angle_deg % 360;
    long sector, idx, val;

    if (a < 0) a += 360;
    if (a > 180) a = 360 - a;  /* симметрия cos */
    if (a > 90)  return -(cos_table[(180 - a) / 15]);

    sector = a / 15;
    if (sector >= 6) return 0;
    idx = a % 15;
    /* линейная интерполяция между точками таблицы */
    val = cos_table[sector] * (15 - idx) / 15
        + cos_table[sector + 1] * idx / 15;
    return val;
}

void calculate_state(long angle, long sunload_x100, long txpower_x10,
                     const SatelliteModel *model, SatelliteState *out)
{
    long cos_val;    /* cos * 1000 */
    long solar_factor; /* 0..1000 */
    long generated;  /* мВт (milliwatts) */
    long consumption;
    long net_power;
    long battery;
    long temp;
    long battery_pct;
    int  temp_ok, tx_ok;

    /* 1. Мощность солнечных панелей */
    cos_val = cos_approx(angle);
    solar_factor = (cos_val > 0) ? cos_val : 0;

    /* generated = solar_max * sunload * MARS_FACTOR * cos / (1000*100*1000) */
    generated = model->solar_max_power / 1000
                * sunload_x100
                * MARS_SOLAR_FACTOR
                * solar_factor
                / (100 * 1000 * 1000);

    if (generated > model->solar_max_power / 1000)
        generated = model->solar_max_power / 1000;

    out->generated_power = generated; /* Вт */

    /* 2. Заряд аккумулятора */
    consumption = txpower_x10 / 10 + 5; /* Вт */
    net_power   = generated - consumption;
    battery     = model->battery_capacity / 1000 / 2
                  + net_power / 2;

    if (battery < 0)
        battery = 0;
    if (battery > model->battery_capacity / 1000)
        battery = model->battery_capacity / 1000;

    out->battery_level = battery; /* Вт·ч */

    /* 3. Температура (в милли-градусах для точности) */
    temp = -100000; /* -100°C * 1000 */
    temp += 120000 * sunload_x100 / 100
                   * MARS_SOLAR_FACTOR / 1000
                   * solar_factor / 1000;
    temp += txpower_x10 * 300 / 10; /* +0.3°C на ватт */

    out->temperature = temp / 1000; /* °C */

    /* 4. Режим работы */
    battery_pct = out->battery_level * 100
                  / (model->battery_capacity / 1000);
    temp_ok = (out->temperature >= model->temp_min / 1000 &&
               out->temperature <= model->temp_max / 1000);
    tx_ok   = (txpower_x10 <= model->tx_power_max / 100);

    if (!temp_ok || !tx_ok || battery_pct < 10)
        out->mode = "CRITICAL";
    else if (battery_pct < 25)
        out->mode = "SAFE";
    else if (battery_pct < 50)
        out->mode = "LOW_POWER";
    else
        out->mode = "NOMINAL";
}
