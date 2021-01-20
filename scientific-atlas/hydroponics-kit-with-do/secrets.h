#define THINGNAME "sensor-XXXXXXXXXXXX"

const char WIFI_SSID[] = "";
const char WIFI_PASSWORD[] = "";
const char AWS_IOT_ENDPOINT[] = "xxxxx.amazonaws.com";

// Topics from dashboard
const char TOPIC_DO[] = "logs/do/XXXXXXX/XXXXX";
const char TOPIC_PH[] = "logs/ph/XXXXXXX/XXXXX";
const char TOPIC_EC[] = "logs/ec/XXXXXXX/XXXXX";
const char TOPIC_TEMP[] = "logs/wt/XXXXXXX/XXXXX";

// Amazon Root CA 1
static const char AWS_CERT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
-----END CERTIFICATE-----
)EOF";

// Device Certificate
static const char AWS_CERT_CRT[] PROGMEM = R"KEY(
-----BEGIN CERTIFICATE-----
-----END CERTIFICATE-----
)KEY";

// Device Private Key
static const char AWS_CERT_PRIVATE[] PROGMEM = R"KEY(
-----BEGIN RSA PRIVATE KEY-----
-----END RSA PRIVATE KEY-----
)KEY";
