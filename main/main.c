/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42): Idea of Jeroen Domburg <jeroen@spritesmods.com>
 *
 * Cornelis wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */

#include <stdio.h>
#include "cJSON.h"
#include "driver/gpio.h"
#include "captdns.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "freertos/event_groups.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/api.h"
#include "lwip/err.h"
#include "nvs_flash.h"
#include "string.h"
#include "tcpip_adapter.h"
#include "lwip/dns.h"

#define LED_GPIO_PIN GPIO_NUM_21
#define LED_BUILTIN 16
#define delay(ms) (vTaskDelay(ms/portTICK_RATE_MS))

static esp_err_t event_handler(void *ctx, system_event_t *event);
static void wifi_AP_init(void);
static EventGroupHandle_t wifi_event_group;

const int CONNECTED_BIT = BIT0;

char* json_unformatted;

const bool embedIndexHTML = true;

const static char http_index_hml[] = "<!DOCTYPE html>"
    "<html>\n"
    "<body>\n"
    "<h1>Hello world, this is " CONFIG_IDF_TARGET "!</h1>\n"
    "<p>Feel free to click the button.</p>\n"
    "<a id=\"myEffect\" href=\"/?state=unknown\">\n"
    "<button id=\"myButton\" class=\"button\">Green Button</button>\n"
    "</a>\n"
    "<script>\n"
    "var x = document.getElementById(\"myButton\");\n"
    "var y = document.getElementById(\"myEffect\");\n"
    "var z = window.location.href;\n"
    "var regex = new RegExp('[?]state=green');\n"
    "x.innerHTML=regex.exec(z);\n"
	"if(regex.exec(z)==\"?state=green\") { x.className=\"button\";  x.innerHTML=\"Green Button\"; y.setAttribute('href','/?state=red');  } else\n"
    "if(regex.exec(z)!=\"?state=green\") { x.className=\"button2\"; x.innerHTML=\"Red Button\"; y.setAttribute('href','/?state=green');  }\n"
    "</script>\n"
    "<style>\n"
    "	.button {\n"
    "  		background-color: green;\n"
    "		border-radius: 10px;\n"
    "		border: none;\n"
    " 		color: black;\n"
    "	}\n"
    "	.button2 {\n"
    "  		background-color: red;\n"
    "		border-radius: 10px;\n"
    "		border: none;\n"
    " 		color: black;\n"
    "	}\n"
    "</style>\n"
    "</body>\n"
    "</html>\n";

const static char http_html_hdr[] = "HTTP/1.1 200 OK\r\nContent-type: text/html\r\n\r\n";
const uint8_t indexHtmlStart[] asm("_binary_index_html_start"); 
const uint8_t indexHtmlEnd[] asm("_binary_index_html_end");     
   
static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    printf("Event = %04X\n", event->event_id);
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        printf("got ip\n");
        printf("ip: " IPSTR "\n", IP2STR(&event->event_info.got_ip.ip_info.ip));
        printf("netmask: " IPSTR "\n", IP2STR(&event->event_info.got_ip.ip_info.netmask));
        printf("gw: " IPSTR "\n", IP2STR(&event->event_info.got_ip.ip_info.gw));
        printf("\n");
        fflush(stdout);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        /* This is a workaround as ESP32 WiFi libs don't currently
           auto-reassociate. */
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void http_server_netconn_serve(struct netconn *conn)
{
  struct netbuf *inbuf;
  char *buf;
  u16_t buflen;
  err_t err;

  /* Read the data from the port, blocking if nothing yet there.
   We assume the request (the part we care about) is in one netbuf */
  err = netconn_recv(conn, &inbuf);

  if (err == ERR_OK) {
    netbuf_data(inbuf, (void**)&buf, &buflen);

    // strncpy(_mBuffer, buf, buflen);

    /* Is this an HTTP GET command? (only check the first 5 chars, since
    there are other formats for GET, and we're keeping it very simple )*/
    printf("buffer = %s \n", buf);
    if (buflen>=5 &&
        buf[0]=='G' &&
        buf[1]=='E' &&
        buf[2]=='T' &&
        buf[3]==' ' &&
        buf[4]=='/' ) {
          printf("buf[5] = %c\n", buf[5]);


       /* React to queries ('?'in buf[5], followed by 'status=green' or 'status=red')
       */

       if(buf[5]=='?') {
        printf("buflen = %d\n", buflen);
        if(buflen>=16 && buf[12]=='g') {
            printf("Query = GREEN\n");
            gpio_set_level(LED_GPIO_PIN, 1);          
        }
        if(buflen>=14 && buf[12]=='r') {
            printf("Query = RED\n");
            gpio_set_level(LED_GPIO_PIN, 0);
        }
       }
 
       /* Send the HTML header
             * subtract 1 from the size, since we dont send the \0 in the string
             * NETCONN_NOCOPY: our data is const static, so no need to copy it
       */
      netconn_write(conn, http_html_hdr, sizeof(http_html_hdr)-1, NETCONN_NOCOPY);

      if(buf[5]=='h') {
        /* Send our HTML page */
        if(embedIndexHTML==false) { netconn_write(conn, http_index_hml, sizeof(http_index_hml)-1, NETCONN_NOCOPY); } 
        else { netconn_write(conn, indexHtmlStart, indexHtmlEnd-indexHtmlStart-1, NETCONN_NOCOPY); }
      }
      else if(buf[5]=='l') {
        /* Send our HTML page */
        if(embedIndexHTML==false) { netconn_write(conn, http_index_hml, sizeof(http_index_hml)-1, NETCONN_NOCOPY); } 
        else { netconn_write(conn, indexHtmlStart, indexHtmlEnd-indexHtmlStart-1, NETCONN_NOCOPY); }

      }
      else if(buf[5]=='j') {
        netconn_write(conn, json_unformatted, strlen(json_unformatted), NETCONN_NOCOPY);
      }
      else {
        if(embedIndexHTML==false) { netconn_write(conn, http_index_hml, sizeof(http_index_hml)-1, NETCONN_NOCOPY); } 
        else { netconn_write(conn, indexHtmlStart, indexHtmlEnd-indexHtmlStart-1, NETCONN_NOCOPY); }
      }
    }

  }
  /* Close the connection (server closes in HTTP) */
  netconn_close(conn);

  /* Delete the buffer (netconn_recv gives us ownership,
   so we have to make sure to deallocate the buffer) */
  netbuf_delete(inbuf);

}

static void http_server(void *pvParameters)
{
  struct netconn *conn, *newconn;
  err_t err;
  conn = netconn_new(NETCONN_TCP);
  netconn_bind(conn, NULL, 80);
  netconn_listen(conn);
  do {
     err = netconn_accept(conn, &newconn);
     if (err == ERR_OK) {
       http_server_netconn_serve(newconn);
       netconn_delete(newconn);
     }
   } while(err == ERR_OK);
   netconn_close(conn);
   netconn_delete(conn);
}


int app_main(void)
{

  gpio_pad_select_gpio(LED_GPIO_PIN);
  gpio_set_direction(LED_GPIO_PIN, GPIO_MODE_OUTPUT);

  wifi_AP_init();
  
  //ip_addr_t dns_addr;
  //IP_ADDR4(&dns_addr, 192,168,4,100);
  //dns_setserver(0, &dns_addr);
  //dns_init();
 
  captdnsInit();
  xTaskCreate(&http_server, "http_server", 2048, NULL, 5, NULL);
  return 0;
}

void wifi_AP_init(void)
{

   nvs_flash_init();
   tcpip_adapter_init();
   
   //tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_AP);

   //tcpip_adapter_ip_info_t ipInfo;
   //IP4_ADDR(&ipInfo.ip, 192,168,4,2);
   //IP4_ADDR(&ipInfo.gw, 192,168,4,1);
   //IP4_ADDR(&ipInfo.netmask, 255,255,255,0);
   //tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_AP, &ipInfo);


   ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
 
   wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
   
   ESP_ERROR_CHECK( esp_wifi_init(&cfg) ); 
   ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
   ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_AP) ); // APSTA) );

   /*wifi_config_t sta_config = {
        .sta = {
            .ssid = "SSID",
            .password = "PASSWORD",
            .bssid_set = false
        }
    };
   ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &sta_config) );
  */
   wifi_config_t apConfig = {
         .ap = {
               .ssid="ESP32",
               .password="test",
               .ssid_len = 0,
               .channel = 6,
               .authmode = WIFI_AUTH_OPEN,
               .ssid_hidden = 0,
               .max_connection=4,
               .beacon_interval = 100
    }
   };

   ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_AP, &apConfig) );
   ESP_ERROR_CHECK( esp_wifi_start() );



}
