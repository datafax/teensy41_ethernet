// stepl's lwIP 2.0.2
// https://forum.pjrc.com/threads/45647-k6x-LAN8720(A)-amp-lwip

#include <time.h>
#include <sys/time.h>
#include "lwip_t41.h"
#include "lwip/inet.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/apps/sntp.h"

#define LOG _printf

#define NTP1 "192.168.1.4"
//#define NTP1 "pool.ntp.org"
#define TZ "EST5EDT" //Timezone (https://github.com/kerlnel/newlib/blob/master/newlib/libc/time/tzset.c)
// !The offset is positive if the local timezone is west of the Prime Meridian and negative if it is east.

#define SEC_PER_HOUR (3600UL)
#define MSEC_PER_SEC (1000UL)
#define USEC_PER_MSEC (1000UL)
#define USEC_PER_SEC (MSEC_PER_SEC * USEC_PER_MSEC)

void _printf(const char* format, ...)
{
    static char buf[256];
    int buf_len;
    struct timeval tv;
    struct tm *p_tm;
    va_list ap;

    buf_len = strlen(buf);
    if (!buf_len || buf[buf_len - 1] == '\n')
    {
        gettimeofday(&tv, NULL);
        p_tm = localtime(&tv.tv_sec);
        Serial.printf("%02d:%02d:%02d.%03d.%03d  ", p_tm->tm_hour, p_tm->tm_min, p_tm->tm_sec, tv.tv_usec / USEC_PER_MSEC % MSEC_PER_SEC, tv.tv_usec % USEC_PER_MSEC);
    }
    va_start(ap, format);
    buf_len = vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);
    Serial.print(buf);
}

#pragma region time

static inline char* ulltoa(unsigned long long val, char *buf, int radix)
{
    char *b, *s, t, d;
    b = s = buf;
    do
    {
        d = val % radix;
        val /= radix;
        *b++ = d + (d < 10 ? '0' : 'A' - 10);
    } while (val > 0);
    *b-- = '\0';
    while (s < b)
    {
        t = *b;
        *b-- = *s;
        *s++ = t;
    }
    return buf;
}

extern "C" {
    extern int _gettimeofday(struct timeval *__tp, void *__tzp);
    extern int settimeofday(const struct timeval *__tp, const struct timezone *__tzp);

    struct
    {
        uint32_t micros;
        uint64_t us;
        uint64_t sync_us;
        int32_t tcv;
    } hw_tm, *p_hw_tm = &hw_tm;

    int _gettimeofday(struct timeval *__tp, void *__tzp)
    {
        uint32_t m = micros();
        uint64_t us = p_hw_tm->us + (m < p_hw_tm->micros ? UINT32_MAX - p_hw_tm->micros + m : m - p_hw_tm->micros);
        p_hw_tm->micros = m;
        p_hw_tm->us = us;
        if (p_hw_tm->tcv)
            us += (p_hw_tm->us - p_hw_tm->sync_us) / p_hw_tm->tcv;
        __tp->tv_sec = us / USEC_PER_SEC;
        __tp->tv_usec = us % USEC_PER_SEC;
        return 0;
    }
        
    int settimeofday(const struct timeval *__tp, const struct timezone *__tzp)
    {
        static char buf1[24], buf2[24];
        static char sign, hw_sign;
        static uint64_t diff_us, diff_hw_us;
        struct timeval tv;
        uint64_t prev_us, prev_hw_us;

        _gettimeofday(&tv, NULL);
        prev_us = (uint64_t)tv.tv_sec * USEC_PER_SEC + tv.tv_usec;
        prev_hw_us = p_hw_tm->us;
        p_hw_tm->us = (uint64_t)__tp->tv_sec * USEC_PER_SEC + __tp->tv_usec;
        setenv("TZ", TZ, 0);

        if (prev_us > p_hw_tm->us)
        {
            sign = -1;
            diff_us = prev_us - p_hw_tm->us;
        }
        else
        {
            sign = 1;
            diff_us = p_hw_tm->us - prev_us;
        }
        
        if (prev_hw_us > p_hw_tm->us)
        {
            hw_sign = -1;
            diff_hw_us = prev_hw_us - p_hw_tm->us;
        }
        else
        {
            hw_sign = 1;
            diff_hw_us = p_hw_tm->us - prev_hw_us;
        }

        if (p_hw_tm->sync_us && diff_hw_us && diff_hw_us < SEC_PER_HOUR * USEC_PER_SEC)
            p_hw_tm->tcv = (p_hw_tm->us - p_hw_tm->sync_us) / diff_hw_us * hw_sign;
        p_hw_tm->sync_us = p_hw_tm->us;

        LOG("time changed: %c%s us (%c%s us, tcv= %c1 every %d us), %s", 
            sign < 0 ? '-' : '+', ulltoa(diff_us, buf1, 10), 
            hw_sign < 0 ? '-' : '+', ulltoa(diff_hw_us, buf2, 10), 
            p_hw_tm->tcv < 0 ? '-' : '+' , abs(p_hw_tm->tcv),
            ctime(&__tp->tv_sec));
        
        return 0;
    }
}

#pragma region lwip

static void netif_status_callback(struct netif *netif)
{
    static char str1[IP4ADDR_STRLEN_MAX], str2[IP4ADDR_STRLEN_MAX], str3[IP4ADDR_STRLEN_MAX];
    LOG("netif status changed: ip %s, mask %s, gw %s\n", ip4addr_ntoa_r(netif_ip_addr4(netif), str1, IP4ADDR_STRLEN_MAX), ip4addr_ntoa_r(netif_ip_netmask4(netif), str2, IP4ADDR_STRLEN_MAX), ip4addr_ntoa_r(netif_ip_gw4(netif), str3, IP4ADDR_STRLEN_MAX));
}

static void link_status_callback(struct netif *netif)
{
    LOG("enet link status: %s\n", netif_is_link_up(netif) ? "up" : "down");
}

#pragma endregion



void setup()
{
    Serial.begin(115200);
    while (!Serial) delay(100);

    enet_init(NULL, NULL, NULL);
    netif_set_status_callback(netif_default, netif_status_callback);
    netif_set_link_callback(netif_default, link_status_callback);
    netif_set_up(netif_default);

    dhcp_start(netif_default);
        
    while (!netif_is_link_up(netif_default)) loop(); // await on link up

#ifdef NTP1
    sntp_setservername(0, (char*)NTP1);
#endif
    sntp_init();
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
