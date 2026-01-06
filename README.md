English.
# astro_switch
An astronomical switch based on Jean Meuss algorithm with an ESP32 and an RTC.

Activates at sunset and deactivates at sunrise.
Internet is not needed. The device creates it's own SoftAP
After you connect Parking AP and your password (123456789) change it, you can access with any browser typing 192.168.4.1 or http://parking.local

Be aware, if you get error compiling ledc, it uses last API (v3.x), previous versions were deprecated

Preparation:
- ESP32 Dev Module
- DS3231 RTC Module
- Solid State Relay (SSR): Connect to GPIO 5 through a resistor. Note: Calculate the resistor value for an 8.5mA current to extend the ESP32's lifespan. Connect to the SSR's positive (+) terminal.
- I2C Connections: Connect SCL/SDA to the ESP32 default pins (GPIO 22 and GPIO 21 respectively).
- Power: Power the module using the 3.3V output and GND.
- Status LED: Connect GPIO 16 to a 330-ohm resistor (anode) and the cathode to GND.

Software:
- Arduino IDE v. 2.3.5 or higher.
- Download the .zip directory.
- Unzip into a folder named astro_switch.
- Launch the Arduino IDE by double-clicking the astro_switch.ino file.
- Required Libraries: Check the #include directives within astro_switch.ino.
- Compile and upload to the ESP32.

Operation:
- Search for the Wi-Fi Access Point (AP) named "Parking".
- Default Password: 123456789
- Open your web browser and navigate to 192.168.4.1.
- Modify the default coordinates to match your current location.
- Adjust the UTC offset for your time zone.
- The system is now ready.

[!IMPORTANT]
NOTE: If the RTC is not connected or the wiring is incorrect, the lights will remain ON constantly as a fail-safe.

OTA (Over-The-Air) Updates:
- Navigate to 192.168.4.1/ota_enable in your browser.
- In the Arduino IDE, wait approximately 20 seconds, then select the port 192.168.4.1.
- Upload the new firmware (the password is the same as the Wi-Fi password).

Additional Available APIs:
- Check the IDE Serial Monitor for details and use your browser to check:
- http://192.168.4.1/ntp_enable: Synchronizes the ESP32 RTC with the network time.
- http://192.168.4.1/ntp_status: Displays the last synchronization time and RTC status.
- http://192.168.4.1/ota_enable: Activates wireless update mode.
- http://192.168.4.1/ota_status: Displays the current OTA status.

LED Behavior:
- Slow Breathing: No clients connected; Wi-Fi power set to 5mW.
- Fast Breathing: Clients connected; Wi-Fi power increases to 100mW.
- Blinking: RTC failure detected.
- Steady ON: Relay activated.
- Note: Wi-Fi power automatically drops to 5mW after disconnecting idle clients.

Español.
# astro_switch

Interruptor astronómico basado en el algoritmo de Jean Meuss con un ESP32 y un RTC.

Se activa a la puesta de sol y desactiva al amanecer.
No necesita conexión a Internet, el dispositivo crea su propia red para configuración.
Después de conectarse al AP y el password (123456789) .. cambiar, puede acceder desde cualquier navegador con 192.168.4.1 o http://parking.local

Ve con cuidado si al compilar te da error, ledc usa la última API (v3.x) las versiones anteriores fueron deshabilitadas.

Preparación:
- ESP32 Dev Module
- Módulo RTC DS3231
- Relé de estado sólido GPIO pin 5 a traves de una resistencia (calcular) para una corriente de 8.5mA (mayor vida para el ESP32) y conectar al + del relé de estado sólido.
- Conectar SCL/SDA a los pines por defecto del ESP32 (21 y 22) 
- Alimentar módulo de la salida de 3.3V y GND
- Led de GPIO 16 + resistencia de 330 ohmios a ánodo led y cátodo a GND.

Software:
- Arduino IDE v. 2.3.5 o superior
- Bajar el directorio .zip
- Descomprimir en una carpeta atro_switch
- Arrancar arduino IDE pulsando doble click sobre astro_switch
- Librerías necesarias (ver #includes en astro_switch.ino)
- Compilar y subir al ESP32.

Funcionamiento:
- Buscar AP 'Parking'
- Passsword por defecto '123456789'
- En el navegador web ir a 192.168.4.1
- Modificar las coordenadas por defecto para que coincidan con tu ubicación. 
- Ajustar UTC de tu zona.
- Y a funcionar.

NOTA: Si no se conecta el RTC o las conexiones no son las correctas estarán siempre las luces encendidas.

Actualizaciones vía OTA.
- Escribir 192.168.4.1/ota_enable
- Buscar en arduino IDE (puertos) 192.168.4.1 al cabo de unos 20 segundos, seleccionar.
- Subir el nuevo firmware (password igual que el de acceso a la wifi)

Otras API's diponibles:
- Ver puerto serial del IDE. 
- http://192.168.4.1/ntp_enable ---> Para sincronizar con red wifi el RTC del ESP32.
- http://192.168.4.1/ntp_status ---> Para conocer la última sincronización y estado del RTC
- http://192.168.4.1/ota_enable ---> Para activar sincronización vía inalámbrica
- http://192.168.4.1/ota_status ---> Para conocer el estado del OTA

Comportamiento del LED:
- Respira lento ---> No hay clientes conectados, Potencia del wifi 5mW.
- Respira rápido --> Hay clientes conectados, Potencia sube a 100mW
- Intermitente ---> Fallo del RTC
- Fijo -----------> Relé activado

NOTA: El Wifi baja a 5mW cuando expulsa a clientes sin iteracciones. 
