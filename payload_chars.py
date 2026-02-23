# --- Configuration (identique au C++) ---
DEVICE_ID = "pico_env_sensor"
DEVICE_NAME = "Pico Env Sensor"
STATE_TOPIC = "pico_env_sensor/state"

# --- Liste des capteurs ---
sensors = [
    {"uid_suffix": "_temp", "val_tpl": "temperature", "unit": "°C",  "dev_cla": "temperature"},
    {"uid_suffix": "_hum",  "val_tpl": "humidity",    "unit": "%",   "dev_cla": "humidity"},
    {"uid_suffix": "_eco2", "val_tpl": "eco2",        "unit": "ppm", "dev_cla": "carbon_dioxide"},
    {"uid_suffix": "_tvoc", "val_tpl": "tvoc",        "unit": "ppb", "dev_cla": "volatile_organic_compounds_parts"},
    {"uid_suffix": "_aqi",  "val_tpl": "aqi",         "unit": "",    "dev_cla": "aqi"},
]

# 1. Construction du device_block
device_block = (
    f'"dev":{{'
        f'"ids":["{DEVICE_ID}"],'
        f'"name":"{DEVICE_NAME}",'
        f'"mf":"DIY",'
        f'"mdl":"Pico W + ENS160 + AHT2x",'
        f'"sw":"1.1",'
        f'"hw":"rev0.85"'
    f'}},'
    f'"avty_t":"{DEVICE_ID}/availability"'
)

# 2. Construction du cmps_block
cmps_parts = []
for s in sensors:
    # Gestion conditionnelle de l'unité
    unit_block = f'"unit_of_meas":"{s["unit"]}",' if s["unit"] else ""
    
    cmp_str = (
        f'"sensor{s["uid_suffix"]}":{{'
            f'"p":"sensor",'
            f'"dev_cla":"{s["dev_cla"]}",'
            f'{unit_block}'
            f'"val_tpl":"{{{{ value_json.{s["val_tpl"]} }}}}",'
            f'"uniq_id":"{DEVICE_ID}{s["uid_suffix"]}"'
        f'}}'
    )
    cmps_parts.append(cmp_str)

cmps_block = '"cmps":{' + ','.join(cmps_parts) + '}'

# 3. Assemblage final du payload
payload = f'{{{device_block},"stat_t":"{STATE_TOPIC}",{cmps_block}}}'

# --- Résultats ---
print("--- PAYLOAD GÉNÉRÉ ---")
print(payload)
print("\n--- STATISTIQUES ---")
print(f"Nombre de caractères (taille exacte) : {len(payload)}")
print(f"Taille recommandée pour le buffer C++ : {len(payload) + 50} (pour la marge de sécurité et le caractère nul '\\0')")