// stepl's lwIP 2.0.2
// https://forum.pjrc.com/threads/45647-k6x-LAN8720(A)-amp-lwip
// SPIFFS version 2  core startup
// https://github.com/PaulStoffregen/teensy41_extram/blob/SPIFFS-FLASH-ONLY


#include <time.h>
#include "lwip_t41.h"
#include "lwip/inet.h"
#include "lwip/dhcp.h"

#include "lwip/apps/tftp_server.h"
#include <spiffs_t4.h>
#include <spiffs.h>

static spiffs_t4 fs; //filesystem
static spiffs_file fd;

#define LOG Serial.printf

static void dateTime(uint16_t* p_date, uint16_t* p_time)
{
  time_t rawtime;
  struct tm *timeinfo;

  time(&rawtime);
  timeinfo = localtime(&rawtime);

  // *p_date = FAT_DATE(timeinfo->tm_year + 1900, timeinfo->tm_mon, timeinfo->tm_mday);
  //  *p_time = FAT_TIME(timeinfo->tm_hour, timeinfo->tm_min, 0);
}

extern "C" {
  extern int _gettimeofday_r(struct _reent *r, struct timeval *__tp, void *__tzp);

  //sd,ftp - set current time for new files, dirs.
  int _gettimeofday_r(struct _reent *r, struct timeval *__tp, void *__tzp)
  {
    //seconds and microseconds since the Epoch
    //__tp->tv_sec = 0;
    //__tp->tv_usec = 0;
    return 0;
  }
}

static void netif_status_callback(struct netif *netif)
{
  static char str1[IP4ADDR_STRLEN_MAX], str2[IP4ADDR_STRLEN_MAX], str3[IP4ADDR_STRLEN_MAX];
  LOG("netif status changed: ip %s, mask %s, gw %s\n", ip4addr_ntoa_r(netif_ip_addr4(netif), str1, IP4ADDR_STRLEN_MAX), ip4addr_ntoa_r(netif_ip_netmask4(netif), str2, IP4ADDR_STRLEN_MAX), ip4addr_ntoa_r(netif_ip_gw4(netif), str3, IP4ADDR_STRLEN_MAX));
}

static void link_status_callback(struct netif *netif)
{
  LOG("enet link status: %s\n", netif_is_link_up(netif) ? "up" : "down");
}


//  SPIFFS interface  open close read write
static uint32_t nbytes, us;

void* tftp_fs_open(const char *fname, const char *mode, uint8_t write)
{
  char  *f = (char *)"xx";

  nbytes = 0;
  us = micros();
  Serial.printf("opening %s  %d \n", fname, write);
  if (write == 0) {
    Serial.println("opening for read");
    fs.f_open(fd, fname, SPIFFS_RDWR);
  }
  else fs.f_open(fd, fname, SPIFFS_CREAT | SPIFFS_TRUNC | SPIFFS_RDWR);
  if (fd) Serial.println("opened");
  else return NULL;

  return f;
}

void tftp_fs_close(void *handle)
{
  us = micros() - us;
  fs.f_close( fd);
  Serial.printf("closed %d bytes %d us\n", nbytes, us);
}

int tftp_fs_read(void *handle, void *buf, int bytes)
{
  int ret = 0;

  ret = fs.f_read( fd, (u8_t *)buf, bytes);
  if (ret > 0)  nbytes += ret;
  else ret = 0;
  return ret;
}

int tftp_fs_write(void *handle, struct pbuf *p)
{
  int ret;

  //  Serial.printf("writing %d bytes\n", p->tot_len);
  nbytes += p->tot_len;
  ret = fs.f_write( fd, (u8_t *)(p->payload), p->tot_len);
  return ret;
}


void setup()
{
  static const tftp_context tftp_ctx = { tftp_fs_open, tftp_fs_close, tftp_fs_read, tftp_fs_write };

  Serial.begin(115200);
  while (!Serial) delay(100);

  // FatFile::dateTimeCallback(dateTime);

  Serial.println("Mount SPIFFS:");
  fs.begin();
  fs.fs_mount();

  enet_init(NULL, NULL, NULL);
  netif_set_status_callback(netif_default, netif_status_callback);
  netif_set_link_callback(netif_default, link_status_callback);
  netif_set_up(netif_default);

  dhcp_start(netif_default);

  while (!netif_is_link_up(netif_default)) loop(); // await on link up
  Serial.println("tftpd server");

  tftp_init(&tftp_ctx);

}

void loop()
{
  static uint32_t last_ms;
  uint32_t ms;

  enet_proc_input();

  ms = millis();
  if (ms - last_ms > 100)
  {
    last_ms = ms;
    enet_poll();
  }
}
