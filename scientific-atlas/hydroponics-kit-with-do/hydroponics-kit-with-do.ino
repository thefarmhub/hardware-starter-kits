#include "secrets.h"
#include <Ezo_i2c.h> // Include the EZO I2C library from https://github.com/Atlas-Scientific/Ezo_I2c_lib
#include <Wire.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <MQTTClient.h>

WiFiClientSecure net = WiFiClientSecure();
MQTTClient client = MQTTClient(256);

Ezo_board PH = Ezo_board(99, "PH");
Ezo_board EC = Ezo_board(100, "EC");
Ezo_board RTD = Ezo_board(102, "RTD");
Ezo_board DO = Ezo_board(97, "DO");

Ezo_board device_list[] = {PH, EC, RTD, DO};

// Enable pins for each circuit
const int EN_PH = 14;
const int EN_EC = 12;
const int EN_RTD = 15;
const int EN_AUX = 13;

// The board we're talking to
Ezo_board *default_board = &device_list[0];

// Gets the length of the array automatically so we dont have to change the number every time we add new boards
const uint8_t device_list_len = sizeof(device_list) / sizeof(device_list[0]);

// How often to publish to Aquaponics AI in milliseconds
const unsigned long publish_delay = 30000;

// The next time we receive a response, in milliseconds
uint32_t next_step_time = 0;

// How long we wait to receive a response, in milliseconds
const unsigned long reading_delay = 1000;

// How long we wait for most commands and queries, in milliseconds
const unsigned long short_delay = 300;

// This is the calculated delay to add to final reading
unsigned int poll_delay = publish_delay - reading_delay * 2 - short_delay;

void connect_wifi()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.println("Connecting to Wi-Fi");

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.print("connected");
}

// A utility function for connecting to Aquaponics AI
void connect_aai()
{
    // Configure WiFiClientSecure to use the AWS IoT device credentials
    net.setCACert(AWS_CERT_CA);
    net.setCertificate(AWS_CERT_CRT);
    net.setPrivateKey(AWS_CERT_PRIVATE);

    // Connect to the MQTT broker on the AWS endpoint we defined earlier
    client.begin(AWS_IOT_ENDPOINT, 8883, net);

    Serial.print("Connecting to Aquaponics AI");

    while (!client.connect(THINGNAME))
    {
        Serial.print(".");
        delay(100);
    }

    if (!client.connected())
    {
        Serial.println("Connection to Aquaponics AI endpoint timed out!");
        return;
    }

    Serial.println("Successfully connected to Aquaponics AI");
}

void setup()
{
    pinMode(EN_PH, OUTPUT);
    pinMode(EN_EC, OUTPUT);
    pinMode(EN_RTD, OUTPUT);
    pinMode(EN_AUX, OUTPUT);
    digitalWrite(EN_PH, LOW);
    digitalWrite(EN_EC, LOW);
    digitalWrite(EN_RTD, HIGH);
    digitalWrite(EN_AUX, LOW);

    Wire.begin();
    Serial.begin(9600);

    connect_wifi();
    connect_aai();
}

// The readings are taken in 3 steps
//   - Step 1: tell the temp sensor to take a reading
//   - Step 2: consume the temp reading and send it to the devices
//   - Step 3: tell the devices to take a reading based on the temp reading we just received
//   - Step 4: consume the devices readings

enum reading_step
{
    REQUEST_TEMP,
    READ_TEMP_AND_COMPENSATE,
    REQUEST_DEVICES,
    READ_RESPONSE
};

// The current step keeps track of where we are.
// Defaults to Step 1
enum reading_step current_step = REQUEST_TEMP;

void loop()
{
    connect_wifi();

    if (millis() >= next_step_time)
    {
        switch (current_step)
        {
        case REQUEST_TEMP:
            RTD.send_read_cmd();
            next_step_time = millis() + reading_delay;
            current_step = READ_TEMP_AND_COMPENSATE;
            break;

        case READ_TEMP_AND_COMPENSATE:
            load_reading(RTD);

            // If the reading is valid
            if ((RTD.get_error() == Ezo_board::SUCCESS) && (RTD.get_last_received_reading() > -1000.0))
            {
                PH.send_cmd_with_num("T,", RTD.get_last_received_reading());
                EC.send_cmd_with_num("T,", RTD.get_last_received_reading());
                DO.send_cmd_with_num("T,", RTD.get_last_received_reading());

                publish(TOPIC_TEMP, String(RTD.get_last_received_reading(), 2))
            }

            // If the temperature reading is invalid
            else
            {
                // Send default temp = 25 deg C to EC sensor
                PH.send_cmd_with_num("T,", 25.0);
                EC.send_cmd_with_num("T,", 25.0);
                DO.send_cmd_with_num("T,", 20.0);
            }

            next_step_time = millis() + short_delay;
            current_step = REQUEST_DEVICES;
            break;

        case REQUEST_DEVICES:
            PH.send_read_cmd();
            EC.send_read_cmd();
            DO.send_read_cmd();

            next_step_time = millis() + reading_delay;
            current_step = READ_RESPONSE;
            break;

        case READ_RESPONSE:
            load_reading(PH);
            if (PH.get_error() == Ezo_board::SUCCESS)
            {
                publish(TOPIC_PH, String(PH.get_last_received_reading(), 2))
            }

            load_reading(EC);
            if (EC.get_error() == Ezo_board::SUCCESS)
            {
                publish(TOPIC_EC, String(EC.get_last_received_reading(), 2))
            }

            load_reading(DO);
            if (DO.get_error() == Ezo_board::SUCCESS)
            {
                publish(TOPIC_DO, String(DO.get_last_received_reading(), 2))
            }

            Serial.println();

            next_step_time = millis() + poll_delay;
            current_step = REQUEST_TEMP;
            break;
        }
    }
}

// Decode the reading after the read command was issued
void load_reading(Ezo_board &Device)
{
    Serial.print(Device.get_name());
    Serial.print(": "); // print the name of the circuit getting the reading

    // Get the response data and put it into the [Device]
    Device.receive_read_cmd();

    print_error_type(Device, String(Device.get_last_received_reading(), 2).c_str());
}

void publish(topic, value)
{
    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.print("Sending to Aquaponics AI: ");

        data = String("{\"v\":");
        data += value;
        data += "}";

        client.publish(topic, data);

        Serial.println("success");
    }
    else
    {
        Serial.println("No Wifi connection, cannot send to Aquaponics AI");
    }
}
