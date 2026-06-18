#ifndef SATELLITES_H
#define SATELLITES_H

#define MAX_MODELS 3

typedef struct {
    const char *model_name;
    const char *parent_body;
    const char *parent_body_type;
    const char *orbit_type;
    const char *operator;
    int         launch_year;
    long        battery_capacity; /* Вт·ч * 1000 */
    long        solar_max_power;  /* Вт * 1000 */
    long        temp_min;         /* °C * 1000 */
    long        temp_max;         /* °C * 1000 */
    long        tx_power_max;     /* Вт * 1000 */
} SatelliteModel;

typedef struct {
    long        battery_level;   /* Вт·ч */
    long        generated_power; /* Вт */
    long        temperature;     /* °C */
    const char *mode;
} SatelliteState;

extern const SatelliteModel SATELLITE_MODELS[MAX_MODELS];

const SatelliteModel *find_model(const char *name);

void calculate_state(long angle, long sunload_x100, long txpower_x10,
                     long t_created, long t_now,
                     const SatelliteModel *model, SatelliteState *out);

#endif
