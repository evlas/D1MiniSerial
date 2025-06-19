# D1MiniSerial - Bridge WiFi per MowerArduino

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Arduino](https://img.shields.io/badge/Arduino-Compatible-00979D.svg?logo=arduino&logoColor=white)](https://www.arduino.cc/)
[![ESP8266](https://img.shields.io/badge/ESP8266-Compatible-E7352C.svg?logo=esphome&logoColor=white)](https://www.espressif.com/)

## 🚜 Descrizione

D1MiniSerial è un bridge WiFi basato su ESP8266 (D1 Mini) che permette il controllo remoto e il monitoraggio di un tagliaerba robotico tramite un'interfaccia web moderna. Questo progetto è stato sviluppato come estensione di [MowerArduino](https://github.com/evlas/MowerArduino), fornendo un'interfaccia di controllo remoto tramite WiFi.

## 🌟 Caratteristiche

- **Interfaccia Web** moderna e reattiva
- **Controllo Remoto** tramite browser web da qualsiasi dispositivo
- **Monitoraggio in Tempo Reale** di stato e batteria
- **Integrazione MQTT** per Home Assistant
- **Mappa** per il tracciamento della posizione
- **Design Responsive** che funziona su dispositivi mobili e desktop
- **Auto-scoperta** in rete locale tramite mDNS

## 🛠 Hardware Richiesto

- Modulo D1 Mini (o compatibile basato su ESP8266)
- Tagliaerba compatibile con [MowerArduino](https://github.com/evlas/MowerArduino)
- Alimentazione stabile 5V
- Connessione WiFi
- (Opzionale) Server MQTT per l'integrazione con Home Assistant

## 📡 Connessioni

| D1 Mini | Collegamento          | Nota                          |
|---------|-----------------------|-------------------------------|
| D2 (GPIO4) | RX del tagliaerba    | Riceve i dati dal tagliaerba  |
| D3 (GPIO0) | TX del tagliaerba    | Invia comandi al tagliaerba   |
| 3.3V   | Alimentazione         | Se necessario                 |
| GND    | GND                   | Terra comune                 |


## 🚀 Installazione

1. **Clona il repository**
   ```bash
   git clone https://github.com/tuoutente/D1MiniSerial.git
   cd D1MiniSerial
   ```

2. **Apri il progetto** con Arduino IDE o PlatformIO

3. **Installa le dipendenze** (se necessario):
   - ESP8266 Core per Arduino
   - PubSubClient (per MQTT)
   - ArduinoJson

4. **Configura** le impostazioni nel file `D1MiniSerial.ino`:
   ```cpp
   const char* ssid = "NOME_RETE_WIFI";
   const char* password = "PASSWORD_WIFI";
   // Configurazione MQTT (opzionale)
   const char* mqtt_server = "homeassistant.local";
   const int mqtt_port = 1883;
   ```

5. **Carica** lo sketch sul tuo D1 Mini

6. **Accedi** all'interfaccia web all'indirizzo `http://d1miniserial.local` o all'IP assegnato

## 🌐 Interfaccia Web

L'interfaccia web include:

- **Pannello di controllo** con stato e comandi
- **Monitoraggio batteria** con percentuale e tensione
- **Mappa** per la posizione del tagliaerba
- **Aggiornamento in tempo reale** dei dati

![Screenshot dell'interfaccia web](mock.html) (Apri mock.html nel browser per vedere un'anteprima)

## 🔌 Integrazione con Home Assistant

Il supporto MQTT permette l'integrazione con Home Assistant. Le entità vengono create automaticamente al primo avvio:

- **Switch** per il controllo del tagliaerba
- **Sensori** per batteria, tensione e stato
- **Auto-scoperta** delle entità

## 📡 Protocollo di Comunicazione

Il modulo comunica con il tagliaerba tramite una porta seriale utilizzando un semplice protocollo basato su stringhe:

```
STATUS:RUNNING,BATT:24.5,PERCENT:75
```

## 🤝 Contributi

I contributi sono ben accetti! Per maggiori dettagli, consulta il file [CONTRIBUTING.md](CONTRIBUTING.md).

## 📄 Licenza

Questo progetto è concesso in licenza con la licenza MIT - vedi il file [LICENSE](LICENSE) per i dettagli.

## 🙏 Ringraziamenti

- [MowerArduino](https://github.com/evlas/MowerArduino) - Il progetto principale a cui questo modulo si collega
- Tutti i contributori che hanno aiutato a migliorare questo progetto

---

<div align="center">
  <p>Realizzato con ❤️ per la comunità open source</p>
  <a href="https://github.com/evlas/MowerArduino">
    <img src="https://img.shields.io/badge/Progetto%20Correlato-MowerArduino-blue" alt="MowerArduino">
  </a>
</div>
