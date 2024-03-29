#include <iot_cmd.h>
#include <WiFiClientSecure.h>
#include <sequencer4.h>
#include <sequencer1.h>
#include <Wire.h>
#include <Ezo_i2c_util.h>
#include <Ezo_i2c.h>      // https://github.com/Atlas-Scientific/Ezo_I2c_lib
#include <PubSubClient.h> // https://github.com/knolleary/pubsubclient

// ------------ Start Configuration ------------ //
const char *WIFI_SSID = "{{- .WiFiSSID -}}";
const char *WIFI_PASSWORD = "{{- .WiFiPassword -}}";

// You can get the following variables from the
// FarmHub dashboard under the "Sensors" tab
const char THINGNAME[] = "{{- .ThingName -}}";
const char TOPIC_PH[] = "{{- .TopicPH -}}";
const char TOPIC_EC[] = "{{- .TopicEC -}}";
const char TOPIC_DO[] = "{{- .TopicDO -}}";
const char TOPIC_TEMP[] = "{{- .TopicTEMP -}}";
const char TOPIC_HUM[] = "{{- .TopicHUM -}}";
const char TOPIC_CO2[] = "{{- .TopicCO2 -}}";

static const char FARMHUB_CERT_CRT[] PROGMEM = R"KEY(
{{ .CertificatePEM }}
)KEY";

static const char FARMHUB_CERT_PRIVATE[] PROGMEM = R"KEY(
{{ .CertificatePrivateKey }}
)KEY";

static const char FARMHUB_CERT_CA[] PROGMEM = R"EOF(
{{ .RootCertificateAuthority }}
)EOF";

const char FARMHUB_IOT_ENDPOINT[] = "{{- .IotEndpoint -}}";
// ------------ End Configuration ------------ //

WiFiClientSecure wifiClient;
PubSubClient pubsubClient(wifiClient);

Ezo_board PH = Ezo_board(99, "PH");
Ezo_board DO = Ezo_board(97, "DO");
Ezo_board RTD = Ezo_board(102, "RTD");
Ezo_board EC = Ezo_board(100, "EC");
Ezo_board PMP = Ezo_board(103, "PMP");
Ezo_board HUM = Ezo_board(111, "HUM");
Ezo_board CO2 = Ezo_board(105, "CO2");

Ezo_board device_list[] = {PH, DO, RTD, EC, HUM, CO2, PMP};
Ezo_board *default_board = &device_list[0];

// gets the length of the array automatically so we dont have to change the number every time we add new boards
const uint8_t device_list_len = sizeof(device_list) / sizeof(device_list[0]);

const int EN_PH = 13;
const int EN_DO = 12;
const int EN_RTD = 33;
const int EN_EC = 27;
const int EN_HUM = 32;
const int EN_CO2 = 15;

const unsigned long reading_delay = 1000;  // how long we wait to receive a response, in milliseconds
const unsigned long farmhub_delay = 15000; // how long we wait to send values to farmhub, in milliseconds

unsigned int poll_delay = 2000 - reading_delay * 2 - 300; // how long to wait between polls after accounting for the times it takes to send readings

// parameters for setting the pump output
#define PUMP_BOARD PMP        // the pump that will do the output (if theres more than one)
#define PUMP_DOSE -0.5        // the dose that the pump will dispense in  milliliters
#define EZO_BOARD EC          // the circuit that will be the target of comparison
#define IS_GREATER_THAN true  // true means the circuit's reading has to be greater than the comparison value, false mean it has to be less than
#define COMPARISON_VALUE 1000 // the threshold above or below which the pump is activated

float k_val = 0;             // holds the k value for determining what to print in the help menu
bool polling = true;         // variable to determine whether or not were polling the circuits
bool send_to_farmhub = true; // variable to determine whether or not were sending data to farmhub

bool wifi_isconnected()
{
    return (WiFi.status() == WL_CONNECTED);
}

void reconnect_wifi()
{
    if (!wifi_isconnected())
    {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        Serial.println("Connecting to WiFi...");
    }
}

void step1(); // forward declarations of functions to use them in the sequencer before defining them
void step2();
void step3();
void step4();
Sequencer4 sensor_sequence(
    &step1, reading_delay,
    &step2, 300,
    &step3, reading_delay,
    &step4, poll_delay);

Sequencer1 wifi_sequence(&reconnect_wifi, 10000);
Sequencer1 farmhub_sequence(&farmhub_send, farmhub_delay);

void setup()
{
    pinMode(EN_PH, OUTPUT);
    pinMode(EN_DO, OUTPUT);
    pinMode(EN_RTD, OUTPUT);
    pinMode(EN_EC, OUTPUT);
    pinMode(EN_HUM, OUTPUT);
    pinMode(EN_CO2, OUTPUT);

    digitalWrite(EN_PH, LOW);
    digitalWrite(EN_DO, LOW);
    digitalWrite(EN_RTD, HIGH);
    digitalWrite(EN_EC, LOW);
    digitalWrite(EN_HUM, HIGH);
    digitalWrite(EN_CO2, HIGH);

    Wire.begin();
    Serial.begin(9600);

    WiFi.mode(WIFI_STA);
    wifi_sequence.reset();
    sensor_sequence.reset();
    farmhub_sequence.reset();

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
        send_to_farmhub = false;
        if (!process_coms(cmd))
        {
            process_command(cmd, device_list, device_list_len, default_board);
        }
    }

    if (polling)
    {
        sensor_sequence.run();
        farmhub_sequence.run();
    }
}

void pump_function(Ezo_board &pump, Ezo_board &sensor, float value, float dose, bool greater_than)
{
    if (sensor.get_error() != Ezo_board::SUCCESS)
        return;

    bool comparison = (greater_than ? (sensor.get_last_received_reading() >= value) : (sensor.get_last_received_reading() <= value));
    if (comparison)
    {
        pump.send_cmd_with_num("d,", dose);
        delay(100);
        Serial.print(pump.get_name());
        Serial.print(" ");

        char response[20];
        Serial.print("pump ");
        Serial.println(pump.receive_cmd(response, 20) == Ezo_board::SUCCESS ? "dispensed" : "error");
    }
    else
    {
        pump.send_cmd("x");
    }
}

void step1()
{
    RTD.send_read_cmd();
}

void step2()
{
    receive_and_print_reading(RTD);

    float temperature = RTD.get_last_received_reading();
    if (RTD.get_error() != Ezo_board::SUCCESS || temperature <= -1000.0)
    {
        temperature = 25.0;
    }

    PH.send_cmd_with_num("T,", temperature);
    EC.send_cmd_with_num("T,", temperature);
    DO.send_cmd_with_num("T,", temperature);

    Serial.print(" ");
}

void step3()
{
    // send a read command. we use this command instead of PH.send_cmd("R");
    // to let the library know to parse the reading
    PH.send_read_cmd();
    EC.send_read_cmd();
    DO.send_read_cmd();
    HUM.send_read_cmd();
    CO2.send_read_cmd();
}

void step4()
{
    receive_and_print_reading(PH);
    Serial.println();

    receive_and_print_reading(EC);
    Serial.println();

    receive_and_print_reading(DO);
    Serial.println();

    receive_and_print_reading(HUM);
    Serial.println();

    receive_and_print_reading(CO2);
    Serial.println();

    pump_function(PUMP_BOARD, EZO_BOARD, COMPARISON_VALUE, PUMP_DOSE, IS_GREATER_THAN);
}

void start_datalogging()
{
    polling = true;
    send_to_farmhub = true;
    farmhub_sequence.reset();
}

bool process_coms(const String &string_buffer)
{
    if (string_buffer == "HELP")
    {
        print_help();
        return true;
    }

    if (string_buffer.startsWith("DATALOG"))
    {
        start_datalogging();
        return true;
    }

    if (string_buffer.startsWith("POLL"))
    {
        polling = true;
        sensor_sequence.reset();

        int16_t index = string_buffer.indexOf(',');
        if (index != -1 && string_buffer.substring(index + 1).toFloat() >= ((reading_delay * 2 + 300) / 1000.0))
        {
            sensor_sequence.set_step4_time((string_buffer.substring(index + 1).toFloat() * 1000.0) - (reading_delay * 2 + 300));
            return true;
        }

        Serial.println("delay too short");
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

void print_help() {
    get_ec_k_value();
    Serial.println(F("Atlas Scientific I2C Aquaponics kit                                       "));
    Serial.println(F("Commands:                                                                  "));
    Serial.println(F("datalog      Takes readings of all sensors every 15 sec send to thingspeak "));
    Serial.println(F("             Entering any commands stops datalog mode.                     "));
    Serial.println(F("poll         Takes readings continuously of all sensors                    "));
    Serial.println(F("                                                                           "));
    Serial.println(F("ph:cal,mid,7     calibrate to pH 7                                         "));
    Serial.println(F("ph:cal,low,4     calibrate to pH 4                                         "));
    Serial.println(F("ph:cal,high,10   calibrate to pH 10                                        "));
    Serial.println(F("ph:cal,clear     clear calibration                                         "));
    Serial.println(F("                                                                           "));
    Serial.println(F("ec:cal,dry           calibrate a dry EC probe                              "));
    Serial.println(F("ec:k,[n]             used to switch K values, standard probes values are 0.1, 1, and 10 "));
    Serial.println(F("ec:cal,clear         clear calibration                                     "));

    if (k_val > 9) {
        Serial.println(F("For K10 probes, these are the recommended calibration values:            "));
        Serial.println(F("  ec:cal,low,12880     calibrate EC probe to 12,880us                    "));
        Serial.println(F("  ec:cal,high,150000   calibrate EC probe to 150,000us                   "));
    }
    else if (k_val > .9) {
        Serial.println(F("For K1 probes, these are the recommended calibration values:             "));
        Serial.println(F("  ec:cal,low,12880     calibrate EC probe to 12,880us                    "));
        Serial.println(F("  ec:cal,high,80000    calibrate EC probe to 80,000us                    "));
    }
    else if (k_val > .09) {
        Serial.println(F("For K0.1 probes, these are the recommended calibration values:           "));
        Serial.println(F("  ec:cal,low,84        calibrate EC probe to 84us                        "));
        Serial.println(F("  ec:cal,high,1413     calibrate EC probe to 1413us                      "));
    }

    Serial.println(F("                                                                           "));
    Serial.println(F("rtd:cal,t            calibrate the temp probe to any temp value            "));
    Serial.println(F("                     t= the temperature you have chosen                    "));
    Serial.println(F("rtd:cal,clear        clear calibration                                     "));
    Serial.println(F("                                                                           "));
    Serial.println(F("do:cal               calibrate DO probe to the air                         "));
    Serial.println(F("do:cal,0             calibrate DO probe to O dissolved oxygen              "));
    Serial.println(F("do:cal,clear         clear calibration                                     "));
}

void farmhub_connect()
{
    while (!pubsubClient.connected())
    {
        Serial.print("Connecting to FarmHub... ");

        wifiClient.setCACert(FARMHUB_CERT_CA);
        wifiClient.setCertificate(FARMHUB_CERT_CRT);
        wifiClient.setPrivateKey(FARMHUB_CERT_PRIVATE);

        pubsubClient.setServer(FARMHUB_IOT_ENDPOINT, 8883);

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

void farmhub_publish(const char *topic, String value)
{
    if (value.isEmpty())
    {
        Serial.println("empty value, skipping");
        return;
    }

    time_t now;
    time(&now);
    char buf[sizeof "2020-01-01T00:00:01Z"];
    strftime(buf, sizeof buf, "%FT%TZ", gmtime(&now));

    String payload = "{\"v\":" + value + ",\"dt\":\"" + buf + "\"}";

    Serial.println(payload);
    pubsubClient.publish(topic, payload.c_str()) ? Serial.println("success") : Serial.println("failed");
}

void farmhub_send()
{
    if (!send_to_farmhub)
        return;

    farmhub_connect();

    if (!wifi_isconnected())
        return;

    Serial.println("Sending data to FarmHub...");

    Serial.print("EC: ");
    if (EC.get_error() == Ezo_board::SUCCESS)
    {
        farmhub_publish(TOPIC_EC, String(EC.get_last_received_reading(), 2));
    }
    else
    {
        Serial.println("reading error");
    }

    Serial.print("PH: ");
    if (PH.get_error() == Ezo_board::SUCCESS)
    {
        farmhub_publish(TOPIC_PH, String(PH.get_last_received_reading(), 2));
    }
    else
    {
        Serial.println("reading error");
    }

    Serial.print("DO: ");
    if (DO.get_error() == Ezo_board::SUCCESS)
    {
        farmhub_publish(TOPIC_DO, String(DO.get_last_received_reading(), 2));
    }
    else
    {
        Serial.println("reading error");
    }

    Serial.print("HUM: ");
    if (HUM.get_error() == Ezo_board::SUCCESS)
    {
        farmhub_publish(TOPIC_HUM, String(HUM.get_last_received_reading(), 2));
    }
    else
    {
        Serial.println("reading error");
    }

    Serial.print("CO2: ");
    if (CO2.get_error() == Ezo_board::SUCCESS)
    {
        farmhub_publish(TOPIC_CO2, String(CO2.get_last_received_reading(), 2));
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
            farmhub_publish(TOPIC_TEMP, String(RTD.get_last_received_reading(), 2));
        }
        else
        {
            farmhub_publish(TOPIC_TEMP, String(25.0));
        }
    }
    else
    {
        Serial.println("reading error");
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
