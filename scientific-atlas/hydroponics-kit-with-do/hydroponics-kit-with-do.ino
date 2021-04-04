#include <iot_cmd.h>
#include <ESP8266WiFi.h>
#include <sequencer4.h>
#include <sequencer1.h>
#include <Wire.h>
#include <Ezo_i2c_util.h>
#include <Ezo_i2c.h>      // https://github.com/Atlas-Scientific/Ezo_I2c_lib
#include <PubSubClient.h> // https://github.com/knolleary/pubsubclient

// ------------ Start Configuration ------------ //
const char* WIFI_SSID = "";
const char* WIFI_PASSWORD = "";
const char THINGNAME[] = "";
const char TOPIC_DO[] = "";
const char TOPIC_PH[] = "";
const char TOPIC_EC[] = "";
const char TOPIC_TEMP[] = "";

static const char AAI_CERT_CRT[] PROGMEM = R"KEY(
)KEY";

static const char AAI_CERT_PRIVATE[] PROGMEM = R"KEY(
)KEY";

static const char AAI_CERT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI
U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs
N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv
o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU
5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy
rqXRfboQnoZsG4q5WTP468SQvvG5
-----END CERTIFICATE-----
)EOF";

const char AAI_IOT_ENDPOINT[] = "a2ymbm509llr2o-ats.iot.us-east-1.amazonaws.com";
// ------------ End Configuration ------------ //

WiFiClientSecure wifiClient;
PubSubClient pubsubClient(wifiClient);

BearSSL::X509List cert(AAI_CERT_CA);
BearSSL::X509List client_crt(AAI_CERT_CRT);
BearSSL::PrivateKey key(AAI_CERT_PRIVATE);

Ezo_board PH = Ezo_board(99, "PH");
Ezo_board EC = Ezo_board(100, "EC");
Ezo_board DO = Ezo_board(97, "DO");
Ezo_board RTD = Ezo_board(102, "RTD");
Ezo_board PMP = Ezo_board(103, "PMP");

Ezo_board device_list[] = {
    PH,
    EC,
    RTD,
    DO,
    PMP};

Ezo_board *default_board = &device_list[0];

const uint8_t device_list_len = sizeof(device_list) / sizeof(device_list[0]);

const int EN_PH = 14;
const int EN_EC = 12;
const int EN_RTD = 15;
const int EN_AUX = 13;

const unsigned long reading_delay = 1000;
const unsigned long aai_delay = 15000;

unsigned int poll_delay = 2000 - reading_delay * 2 - 300;

#define PUMP_BOARD PMP
#define PUMP_DOSE -0.5
#define EZO_BOARD EC
#define IS_GREATER_THAN true
#define COMPARISON_VALUE 1000

float k_val = 0;

bool polling = true;
bool send_to_aai = true;

bool wifi_isconnected()
{
    return (WiFi.status() == WL_CONNECTED);
}

void reconnect_wifi()
{
    if (!wifi_isconnected())
    {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        Serial.println("connecting to wifi");
    }
}

void step1();
void step2();
void step3();
void step4();
Sequencer4 sensor_sequence(&step1, reading_delay,
                           &step2, 300,
                           &step3, reading_delay,
                           &step4, poll_delay);

Sequencer1 wifi_sequence(&reconnect_wifi, 10000);
Sequencer1 aai_sequence(&aai_send, aai_delay);

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

    WiFi.mode(WIFI_STA);

    wifi_sequence.reset();
    sensor_sequence.reset();
    aai_sequence.reset();

    reconnect_wifi();
    set_clock();
}

void loop()
{
    String cmd;

    wifi_sequence.run();

    if (receive_command(cmd))
    {
        polling = false;
        send_to_aai = false;
        if (!process_coms(cmd))
        {
            process_command(cmd, device_list, device_list_len, default_board);
        }
    }

    if (polling == true)
    {
        sensor_sequence.run();
        aai_sequence.run();
    }
}

void pump_function(Ezo_board &pump, Ezo_board &sensor, float value, float dose, bool greater_than)
{
    if (sensor.get_error() == Ezo_board::SUCCESS)
    {
        bool comparison = false;
        if (greater_than)
        {
            comparison = (sensor.get_last_received_reading() >= value);
        }
        else
        {
            comparison = (sensor.get_last_received_reading() <= value);
        }
        if (comparison)
        {
            pump.send_cmd_with_num("d,", dose);
            delay(100);
            Serial.print(pump.get_name());
            Serial.print(" ");
            char response[20];
            if (pump.receive_cmd(response, 20) == Ezo_board::SUCCESS)
            {
                Serial.print("pump dispensed ");
            }
            else
            {
                Serial.print("pump error ");
            }
            Serial.println(response);
        }
        else
        {
            pump.send_cmd("x");
        }
    }
}

void step1()
{

    RTD.send_read_cmd();
}

void step2()
{
    receive_and_print_reading(RTD);

    if ((RTD.get_error() == Ezo_board::SUCCESS) && (RTD.get_last_received_reading() > -1000.0))
    {
        PH.send_cmd_with_num("T,", RTD.get_last_received_reading());
        EC.send_cmd_with_num("T,", RTD.get_last_received_reading());
        DO.send_cmd_with_num("T,", RTD.get_last_received_reading());
    }
    else
    {
        PH.send_cmd_with_num("T,", 25.0);
        EC.send_cmd_with_num("T,", 25.0);
        DO.send_cmd_with_num("T,", 20.0);
    }

    Serial.print(" ");
}

void step3()
{
    PH.send_read_cmd();
    EC.send_read_cmd();
    DO.send_read_cmd();
}

void step4()
{
    receive_and_print_reading(PH);

    Serial.print("  ");
    receive_and_print_reading(EC);

    Serial.print("  ");
    receive_and_print_reading(DO);

    Serial.println();
    pump_function(PUMP_BOARD, EZO_BOARD, COMPARISON_VALUE, PUMP_DOSE, IS_GREATER_THAN);
}

void start_datalogging()
{
    polling = true;
    send_to_aai = true;
    aai_sequence.reset();
}

bool process_coms(const String &string_buffer)
{
    if (string_buffer == "HELP")
    {
        print_help();
        return true;
    }
    else if (string_buffer.startsWith("DATALOG"))
    {
        start_datalogging();
        return true;
    }
    else if (string_buffer.startsWith("POLL"))
    {
        polling = true;
        sensor_sequence.reset();

        int16_t index = string_buffer.indexOf(',');
        if (index != -1)
        {
            float new_delay = string_buffer.substring(index + 1).toFloat();

            float mintime = reading_delay * 2 + 300;
            if (new_delay >= (mintime / 1000.0))
            {
                sensor_sequence.set_step4_time((new_delay * 1000.0) - mintime);
            }
            else
            {
                Serial.println("delay too short");
            }
        }
        return true;
    }
    return false;
}

void get_ec_k_value()
{
    char rx_buf[10];
    EC.send_cmd("k,?");
    delay(300);
    if (EC.receive_cmd(rx_buf, 10) == Ezo_board::SUCCESS)
    {
        k_val = String(rx_buf).substring(3).toFloat();
    }
}

void print_help()
{
    get_ec_k_value();
    Serial.println(F("Atlas Scientific I2C hydroponics kit                                       "));
    Serial.println(F("Commands:                                                                  "));
    Serial.println(F("datalog      Takes readings of all sensors every 15 sec send to Aquaponics AI "));
    Serial.println(F("             Entering any commands stops datalog mode.                     "));
    Serial.println(F("poll         Takes readings continuously of all sensors                    "));
    Serial.println(F("                                                                           "));
    Serial.println(F("ph:cal,mid,7     calibrate to pH 7                                         "));
    Serial.println(F("ph:cal,low,4     calibrate to pH 4                                         "));
    Serial.println(F("ph:cal,high,10   calibrate to pH 10                                        "));
    Serial.println(F("ph:cal,clear     clear calibration                                         "));
    Serial.println(F("                                                                           "));
    Serial.println(F("do:cal               calibrate DO probe to the air                         "));
    Serial.println(F("do:cal,0             calibrate DO probe to O dissolved oxygen              "));
    Serial.println(F("do:cal,clear         clear calibration                                     "));
    Serial.println(F("                                                                           "));
    Serial.println(F("ec:cal,dry           calibrate a dry EC probe                              "));
    Serial.println(F("ec:k,[n]             used to switch K values, standard probes values are 0.1, 1, and 10 "));
    Serial.println(F("ec:cal,clear         clear calibration                                     "));

    if (k_val > 9)
    {
        Serial.println(F("For K10 probes, these are the recommended calibration values:            "));
        Serial.println(F("  ec:cal,low,12880     calibrate EC probe to 12,880us                    "));
        Serial.println(F("  ec:cal,high,150000   calibrate EC probe to 150,000us                   "));
    }
    else if (k_val > .9)
    {
        Serial.println(F("For K1 probes, these are the recommended calibration values:             "));
        Serial.println(F("  ec:cal,low,12880     calibrate EC probe to 12,880us                    "));
        Serial.println(F("  ec:cal,high,80000    calibrate EC probe to 80,000us                    "));
    }
    else if (k_val > .09)
    {
        Serial.println(F("For K0.1 probes, these are the recommended calibration values:           "));
        Serial.println(F("  ec:cal,low,84        calibrate EC probe to 84us                        "));
        Serial.println(F("  ec:cal,high,1413     calibrate EC probe to 1413us                      "));
    }

    Serial.println(F("                                                                           "));
    Serial.println(F("rtd:cal,t            calibrate the temp probe to any temp value            "));
    Serial.println(F("                     t= the temperature you have chosen                    "));
    Serial.println(F("rtd:cal,clear        clear calibration                                     "));
}

void aai_connect()
{
    while (!pubsubClient.connected())
    {
        Serial.print("Connecting to Aquaponics AI... ");

        wifiClient.setTrustAnchors(&cert);
        wifiClient.setClientRSACert(&client_crt, &key);

        pubsubClient.setServer(AAI_IOT_ENDPOINT, 8883);

        if (pubsubClient.connect(THINGNAME))
        {
            Serial.println("success");
        }
        else
        {
            Serial.print("failed, rc=");
            Serial.print(pubsubClient.state());
            Serial.println(" trying again in 5 seconds");

            delay(5000);
        }
    }
}

void aai_publish(const char *topic, String value)
{
    if (value == "")
    {
        Serial.println("failed [empty value]");
        return;
    }

    time_t now;
    time(&now);
    char buf[sizeof "2020-01-01T00:00:01Z"];
    strftime(buf, sizeof buf, "%FT%TZ", gmtime(&now));
    
    String payload = "{\"v\":" + value + ",\"dt\":\"" + buf + "\"}";
    Serial.println(payload);
    if (pubsubClient.publish(topic, payload.c_str()))
    {
        Serial.println("success");
    }
    else
    {
        Serial.println("failed");
    }
}

void aai_send()
{
    if (send_to_aai == true)
    {
        aai_connect();

        if (wifi_isconnected())
        {
            Serial.println("Sending data to Aquaponics AI...");

            Serial.print("EC: ");
            if (EC.get_error() == Ezo_board::SUCCESS)
            {
                aai_publish(TOPIC_EC, String(EC.get_last_received_reading(), 2));
            }
            else
            {
                Serial.println("reading error");
            }

            Serial.print("PH: ");
            if (PH.get_error() == Ezo_board::SUCCESS)
            {
                aai_publish(TOPIC_PH, String(PH.get_last_received_reading(), 2));
            }
            else
            {
                Serial.println("reading error");
            }

            Serial.print("DO: ");
            if (DO.get_error() == Ezo_board::SUCCESS)
            {
                aai_publish(TOPIC_DO, String(DO.get_last_received_reading(), 2));
            }
            else
            {
                Serial.println("reading error");
            }

            Serial.print("RTD: ");
            if (RTD.get_error() == Ezo_board::SUCCESS)
            {
                if (RTD.get_last_received_reading() > -1000.0)
                {
                    aai_publish(TOPIC_TEMP, String(RTD.get_last_received_reading(), 2));
                }
                else
                {
                    aai_publish(TOPIC_TEMP, String(25.0));
                }
            }
            else
            {
                Serial.println("reading error");
            }
        }
    }
}

void set_clock()
{
    configTime(3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    Serial.print("Waiting for NTP time sync: ");
    time_t now = time(nullptr);
    while (now < 8 * 3600 * 2)
    {
        delay(500);
        Serial.print(".");
        now = time(nullptr);
    }
    Serial.println("");
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    Serial.print("Current time: ");
    Serial.print(asctime(&timeinfo));
}
