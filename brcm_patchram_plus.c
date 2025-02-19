/*******************************************************************************
 *
 *  Copyright (C) 2009-2011 Broadcom Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/

// TODO: Integrate BCM support into Bluez hciattach

#include <errno.h>
#include <getopt.h>
#include <stdio.h>

#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef ANDROID
#include <termios.h>
#else
#include <limits.h>
#include <sys/ioctl.h>
#include <sys/termios.h>
#endif

#include <ctype.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "daemonize.h"

#ifdef ANDROID
#include <cutils/properties.h>
#define LOG_TAG "brcm_patchram_plus"
#include <cutils/log.h>
#undef printf
#define printf ALOGD
#undef fprintf
#define fprintf(x, ...)              \
    {                                \
        if (x == stderr)             \
            ALOGE(__VA_ARGS__);      \
        else                         \
            fprintf(x, __VA_ARGS__); \
    }

#endif // ANDROID

#ifndef N_HCI
#define N_HCI 15
#endif

// from kernel 4.4.176
#define HCIUARTSETPROTO _IOW('U', 200, int)
#define HCIUARTGETPROTO _IOR('U', 201, int)
#define HCIUARTGETDEVICE _IOR('U', 202, int)
#define HCIUARTSETFLAGS _IOW('U', 203, int)
#define HCIUARTGETFLAGS _IOR('U', 204, int)

// from kernel 4.4.176
#define HCI_UART_H4 0
#define HCI_UART_BCSP 1
#define HCI_UART_3WIRE 2
#define HCI_UART_H4DS 3
#define HCI_UART_LL 4
#define HCI_UART_ATH3K 5
#define HCI_UART_INTEL 6
#define HCI_UART_BCM 7
#define HCI_UART_QCA 8

#define HCI_EVT_CMD_CMPL_LOCAL_NAME_STRING 6

#define LOCAL_NAME_BUFFER_LEN 32
#define BUFFER_SIZE 1024

typedef unsigned char uchar;

/* AMPAK FW auto detection table */
typedef struct {
    const char* chip_id;
    const char* updated_chip_id;
} fw_auto_detection_entry_t;

#define FW_TABLE_VERSION "v1.1 20161117"

/* Chip name from HCI | Firmware file name */
static const fw_auto_detection_entry_t fw_auto_detection_table[] = {
    { "4343A0", "BCM43438A0" }, // AP6212
    { "BCM43430A1", "BCM43438A1" }, // AP6212A The BCM Chip is wrong labled. It called BCM43438A1
    { "BCM20702A", "BCM20710A1" }, // AP6210B
    { "BCM4335C0", "BCM4339A0" }, // AP6335
    { "BCM4330B1", "BCM40183B2" }, // AP6330
    { "BCM4324B3", "BCM43241B4" }, // AP62X2
    { "BCM4350C0", "BCM4354A1" }, // AP6354
    { "BCM4354A2", "BCM4356A2" }, // AP6356
    //    {"BCM4345C0","BCM4345C0"}, //AP6255
    //    {"BCM43341B0","BCM43341B0"}, //AP6234
    //    {"BCM2076B1","BCM2076B1"}, //AP6476
    { "BCM43430B0", "BCM4343B0" }, // AP6236
    { "BCM4359C0", "BCM4359C0" }, // AP6359
    { "BCM4349B1", "BCM4359B1" }, // AP6359
    { (const char*)NULL, NULL }
};

int uart_fd = -1;
int hcdfile_fd = -1;
int termios_baudrate = 0;
int bdaddr_flag = 0;
int enable_lpm = 0;
int enable_hci = 0;
int use_baudrate_for_download = 0;
int debug = 0;
int scopcm = 0;
int i2s = 0;
int no2bytes = 0;
int tosleep = 0;

struct termios termios;
uchar buffer[BUFFER_SIZE];
uchar local_name[LOCAL_NAME_BUFFER_LEN];
uchar fw_folder_path[BUFFER_SIZE];

uchar hci_reset[] = {
    0x01, 0x03, 0x0c, 0x00
};

uchar hci_read_local_name[] = {
    0x01, 0x14, 0x0c, 0x00
};

uchar hci_download_minidriver[] = {
    0x01, 0x2e, 0xfc, 0x00
};

uchar hci_update_baud_rate[] = {
    0x01, 0x18, 0xfc, 0x06,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    /* ^ space for baudrate ^ */
};

uchar hci_write_bd_addr[] = {
    0x01, 0x01, 0xfc, 0x06,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    /* ^ space for BT MAC address ^ */
};

uchar hci_write_sleep_mode[] = {
    0x01, 0x27, 0xfc, 0x0c, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00,
    0x00
};

uchar hci_write_sco_pcm_int[] = {
    0x01, 0x1C, 0xFC, 0x05, 0x00,
    0x00, 0x00, 0x00, 0x00
};

uchar hci_write_pcm_data_format[] = {
    0x01, 0x1e, 0xFC, 0x05, 0x00,
    0x00, 0x00, 0x00, 0x00
};

uchar hci_write_i2spcm_interface_param[] = {
    0x01, 0x6d, 0xFC, 0x04, 0x00,
    0x00, 0x00, 0x00
};

/*
#define HCI_OP_READ_LOCAL_VERSION	0x1001
struct hci_rp_read_local_version {
        __u8     status;
        __u8     hci_ver;
        __le16   hci_rev;
        __u8     lmp_ver;
        __le16   manufacturer;
        __le16   lmp_subver;
} __packed;
*/

//{{ add by FriendlyARM
static int _debug = 1;

#define LOG_FILE_NAME "/tmp/brcm_patchram_plus.log"

static void _log2file(const char* fmt, va_list vl)
{
    FILE* file_out;
    file_out = fopen(LOG_FILE_NAME, "a+");
    if (file_out == NULL) {
        return;
    }
    vfprintf(file_out, fmt, vl);
    fclose(file_out);
}

void log2file(const char* fmt, ...)
{
    if (_debug) {
        va_list vl;
        va_start(vl, fmt);
        _log2file(fmt, vl);
        va_end(vl);
    }
}
//}}

int parse_patchram(char* optarg)
{
    int len = strlen(optarg);
    char* path = (char*)&fw_folder_path[0];
    char* p = optarg + len - 1;

    /*Look for first '/' to know the fw path*/
    while (len > 0) {
        if (*p == '/')
            break;
        len--;
        p--;
    }

    if (len > 0) {
        *p = 0;
        strncpy(path, optarg, BUFFER_SIZE - 1);
        log2file("FW folder path = %s\n", fw_folder_path);
    }
#if 0
	char *p;

	if (!(p = strrchr(optarg, '.'))) {
		log2file("file %s not an HCD file\n", optarg);
		exit(3);
	}

	p++;

	if (strcasecmp("hcd", p) != 0) {
		log2file("file %s not an HCD file\n", optarg);
		exit(4);
	}

	if ((hcdfile_fd = open(optarg, O_RDONLY)) == -1) {
		log2file("file %s could not be opened, error %d\n", optarg, errno);
		exit(5);
	}
#endif
    return (0);
}

void BRCM_encode_baud_rate(uint baud_rate, uchar* encoded_baud)
{
    if (baud_rate == 0 || encoded_baud == NULL) {
        log2file("Baudrate not supported!");
        return;
    }

    encoded_baud[3] = (uchar)(baud_rate >> 24);
    encoded_baud[2] = (uchar)(baud_rate >> 16);
    encoded_baud[1] = (uchar)(baud_rate >> 8);
    encoded_baud[0] = (uchar)(baud_rate & 0xFF);
}

typedef struct {
    int baud_rate;
    int termios_value;
} tBaudRates;

tBaudRates baud_rates[] = {
    { 115200, B115200 },
    { 230400, B230400 },
#ifdef __linux
    { 460800, B460800 },
    { 500000, B500000 },
    { 576000, B576000 },
    { 921600, B921600 },
    { 1000000, B1000000 },
    { 1152000, B1152000 },
    { 1500000, B1500000 },
    { 2000000, B2000000 },
    { 2500000, B2500000 },
    { 3000000, B3000000 },
#ifndef __CYGWIN__
    { 3500000, B3500000 },
    { 4000000, B4000000 }
#endif
#endif
};

int validate_baudrate(int baud_rate, int* value)
{
    unsigned int i;

    for (i = 0; i < (sizeof(baud_rates) / sizeof(tBaudRates)); i++) {
        if (baud_rates[i].baud_rate == baud_rate) {
            *value = baud_rates[i].termios_value;
            return (1);
        }
    }

    return (0);
}

int parse_baudrate(char* optarg)
{
    int baudrate = atoi(optarg);

    if (validate_baudrate(baudrate, &termios_baudrate)) {
        BRCM_encode_baud_rate(baudrate, &hci_update_baud_rate[6]);
    }

    return (0);
}

int parse_bdaddr(char* optarg)
{
    int bd_addr[6];
    int ret, i;

    ret = sscanf(optarg, "%02X:%02X:%02X:%02X:%02X:%02X",
        &bd_addr[5], &bd_addr[4], &bd_addr[3],
        &bd_addr[2], &bd_addr[1], &bd_addr[0]);

    if (ret != 6) {
        return (1);
    }

    for (i = 0; i < 6; i++) {
        hci_write_bd_addr[4 + i] = bd_addr[i];
    }

    bdaddr_flag = 1;

    return (0);
}

int parse_enable_lpm(char* optarg)
{
    enable_lpm = 1;
    return (0);
}

int parse_use_baudrate_for_download(char* optarg)
{
    use_baudrate_for_download = 1;
    return (0);
}

int parse_enable_hci(char* optarg)
{
    enable_hci = 1;
    return (0);
}

int parse_scopcm(char* optarg)
{
    int param[10];
    int ret;
    int i;

    ret = sscanf(optarg, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
        &param[0], &param[1], &param[2], &param[3], &param[4],
        &param[5], &param[6], &param[7], &param[8], &param[9]);

    if (ret != 10) {
        return (1);
    }

    scopcm = 1;

    for (i = 0; i < 5; i++) {
        hci_write_sco_pcm_int[4 + i] = param[i];
    }

    for (i = 0; i < 5; i++) {
        hci_write_pcm_data_format[4 + i] = param[5 + i];
    }

    return (0);
}

int parse_i2s(char* optarg)
{
    int param[4];
    int ret;
    int i;

    ret = sscanf(optarg, "%d,%d,%d,%d",
        &param[0], &param[1], &param[2], &param[3]);

    if (ret != 4) {
        return (1);
    }

    i2s = 1;

    for (i = 0; i < 4; i++) {
        hci_write_i2spcm_interface_param[4 + i] = param[i];
    }

    return (0);
}

int parse_no2bytes(char* optarg)
{
    no2bytes = 1;
    return (0);
}

int parse_tosleep(char* optarg)
{
    tosleep = atoi(optarg);

    if (tosleep <= 0) {
        return (1);
    }

    return (0);
}

void usage(char* argv0)
{
    printf("Usage %s:\n", argv0);
    printf("\t<-d> to print a debug log\n");
    printf("\t<--patchram patchram_file>\n");
    printf("\t<--baudrate baud_rate>\n");
    printf("\t<--bd_addr bd_address>\n");
    printf("\t<--enable_lpm>\n");
    printf("\t<--enable_hci>\n");
    printf("\t<--use_baudrate_for_download> - Uses the\n");
    printf("\t\tbaudrate for downloading the firmware\n");
    printf("\t<--scopcm=sco_routing,pcm_interface_rate,frame_type,\n");
    printf("\t\tsync_mode,clock_mode,lsb_first,fill_bits,\n");
    printf("\t\tfill_method,fill_num,right_justify>\n");
    printf("\n\t\tWhere\n");
    printf("\n\t\tsco_routing is 0 for PCM, 1 for Transport,\n");
    printf("\t\t2 for Codec and 3 for I2S,\n");
    printf("\n\t\tpcm_interface_rate is 0 for 128KBps, 1 for\n");
    printf("\t\t256 KBps, 2 for 512KBps, 3 for 1024KBps,\n");
    printf("\t\tand 4 for 2048Kbps,\n");
    printf("\n\t\tframe_type is 0 for short and 1 for long,\n");
    printf("\t\tsync_mode is 0 for slave and 1 for master,\n");
    printf("\n\t\tclock_mode is 0 for slabe and 1 for master,\n");
    printf("\n\t\tlsb_first is 0 for false aand 1 for true,\n");
    printf("\n\t\tfill_bits is the value in decimal for unused bits,\n");
    printf("\n\t\tfill_method is 0 for 0's and 1 for 1's, 2 for\n");
    printf("\t\tsigned and 3 for programmable,\n");
    printf("\n\t\tfill_num is the number or bits to fill,\n");
    printf("\n\t\tright_justify is 0 for false and 1 for true\n");
    printf("\n\t<--i2s=i2s_enable,is_master,sample_rate,clock_rate>\n");
    printf("\n\t\tWhere\n");
    printf("\n\t\ti2s_enable is 0 for disable and 1 for enable,\n");
    printf("\n\t\tis_master is 0 for slave and 1 for master,\n");
    printf("\n\t\tsample_rate is 0 for 8KHz, 1 for 16Khz and\n");
    printf("\t\t2 for 4 KHz,\n");
    printf("\n\t\tclock_rate is 0 for 128KHz, 1 for 256KHz, 3 for\n");
    printf("\t\t1024 KHz and 4 for 2048 KHz.\n\n");
    printf("\t<--no2bytes skips waiting for two byte confirmation\n");
    printf("\t\tbefore starting patchram download. Newer chips\n");
    printf("\t\tdo not generate these two bytes.>\n");
    printf("\t<--tosleep=microseconds>\n");
    printf("\tuart_device_name\n");
}

int parse_cmd_line(int argc, char** argv)
{
    int c;
    int ret = 0;
    int port_ind = argc - 1;

    /* parsers */
    typedef int (*PFI)();
    PFI parse[] = {
        parse_patchram,
        parse_baudrate,
        parse_bdaddr,
        parse_enable_lpm,
        parse_enable_hci,
        parse_use_baudrate_for_download,
        parse_scopcm,
        parse_i2s,
        parse_no2bytes,
        parse_tosleep
    };

    while (1) {
        int option_index = 0;

        static struct option long_options[] = {
            { "patchram", 1, 0, 0 },
            { "baudrate", 1, 0, 0 },
            { "bd_addr", 1, 0, 0 },
            { "enable_lpm", 0, 0, 0 },
            { "enable_hci", 0, 0, 0 },
            { "use_baudrate_for_download", 0, 0, 0 },
            { "scopcm", 1, 0, 0 },
            { "i2s", 1, 0, 0 },
            { "no2bytes", 0, 0, 0 },
            { "tosleep", 1, 0, 0 },
            { 0, 0, 0, 0 }
        };

        c = getopt_long_only(argc, argv,
            "d", long_options, &option_index);

        if (c == -1) {
            break;
        }

        switch (c) {
            case 0:
                if (debug) {
                    log2file("option %s",
                        long_options[option_index].name);
                    if (optarg)
                        log2file(" with arg %s", optarg);
                    log2file("\n");
                }

                ret = (*parse[option_index])(optarg);

                break;
            case 'd':
                debug = 1;
                break;

            case '?':
                // nobreak
            default:
                usage(argv[0]);
                break;
        }

        if (ret) {
            usage(argv[0]);
            break;
        }
    }

    if (ret) {
        return (1);
    }

    log2file("port_ind: %d argc: %d\n", port_ind, argc);

    if (port_ind < argc) {
        if (debug)
            log2file("uart: %s\n", argv[port_ind]);

        if ((uart_fd = open(argv[port_ind], O_RDWR | O_NOCTTY)) == -1) {
            log2file("port %s could not be opened, error %d\n",
                argv[port_ind], errno);
        }
    }

    return (0);
}

int init_uart(struct termios* termios)
{
    int ret;

    log2file("init_uart:\n");

    /* cleanup uart buffers */
    if (tcflush(uart_fd, TCIOFLUSH)) {
        log2file("Failed to flush uart:\n");
        return -EIO;
    }

    /* get current terminal settings */
    if (tcgetattr(uart_fd, termios)) {
        log2file("Failed to get terminal settings:\n");
        return -EIO;
    }

#ifndef __CYGWIN__
    cfmakeraw(termios);
#else
    termios.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
                         | INLCR | IGNCR | ICRNL | IXON);
    termios.c_oflag &= ~OPOST;
    termios.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    termios.c_cflag &= ~(CSIZE | PARENB);
    termios.c_cflag |= CS8;
#endif

    /* set hardware flow control */
    termios->c_cflag |= CRTSCTS;
    ret = tcsetattr(uart_fd, TCSANOW, termios);
    ret |= tcflush(uart_fd, TCIOFLUSH);
    if (ret) {
        log2file("Failed to set hardware flow control:\n");
        return -EIO;
    }

#if 0
    ret = tcsetattr(uart_fd, TCSANOW, termios);
    ret |= tcflush(uart_fd, TCIOFLUSH);
    if (ret) {
        log2file("Failed to set hardware flow control:\n");
        return -EIO;
    }
    // tcflush(uart_fd, TCIOFLUSH);
#endif

    cfsetospeed(termios, B115200);
    // cfsetispeed(&termios, B115200);

    tcsetattr(uart_fd, TCSANOW, termios);
    return 0;
}

void dump(uchar* out, int len)
{
    int i;

    for (i = 0; i < len; i++) {
        if (i && !(i % 16)) {
            log2file("\n");
        }

        log2file("%02x ", out[i]);
    }

    log2file("\n");
}

void read_event(int fd, uchar* buffer)
{
    int i = 0;
    int len = 3;
    int count;

    while ((count = read(fd, &buffer[i], len)) < len) {
        i += count;
        len -= count;
    }

    i += count;
    len = buffer[2];

    while ((count = read(fd, &buffer[i], len)) < len) {
        i += count;
        len -= count;
    }

    if (debug) {
        count += i;

        log2file("received %d\n", count);
        dump(buffer, count);
    }
}

void hci_send_cmd(uchar* buf, int len)
{
    if (debug) {
        log2file("writing\n");
        dump(buf, len);
    }

    if (write(uart_fd, buf, len) != len) {
        log2file("Failed to send HCI command.\n");
        exit(5);
    }
}

void expired(int sig)
{
    log2file("Command TIMED OUT: try reset chip.\n");
    hci_send_cmd(hci_reset, sizeof(hci_reset));
    alarm(4);
}

void proc_reset()
{
    signal(SIGALRM, expired);

    log2file("proc_reset: send hci_reset\n");
    hci_send_cmd(hci_reset, sizeof(hci_reset));
    alarm(4);

    log2file("proc_reset: read resp. event\n");
    read_event(uart_fd, buffer);
    alarm(0);
}

void proc_read_local_name()
{
    int i;
    char* p_name;
    log2file("proc_read_local_name:\n");

    hci_send_cmd(hci_read_local_name, sizeof(hci_read_local_name));
    read_event(uart_fd, buffer);

    p_name = (char*)(&buffer[1 + HCI_EVT_CMD_CMPL_LOCAL_NAME_STRING]);
    for (i = 0; (i < LOCAL_NAME_BUFFER_LEN) || (*(p_name + i) != 0); i++)
        *(p_name + i) = toupper((*(p_name + i)));

    strncpy((char*)&local_name[0], p_name, LOCAL_NAME_BUFFER_LEN - 1);
    log2file("chip id = %s\n", local_name);
}

void proc_open_patchram()
{
    fw_auto_detection_entry_t* p_entry;
    char fw_path[BUFFER_SIZE];
    char* p;
    int i;

    log2file("proc_open_patchram:\n");

    p_entry = (fw_auto_detection_entry_t*)fw_auto_detection_table;
    while (p_entry->chip_id != NULL) {
        log2file("%s %s\n", local_name, p_entry->chip_id);
        if (strstr((const char*)local_name, p_entry->chip_id) != NULL) {
            strncpy((char*)local_name, p_entry->updated_chip_id, sizeof(local_name) - 1);
            break;
        }
        p_entry++;
    }

    snprintf(fw_path, BUFFER_SIZE - 1, "%s/%s.HCD", fw_folder_path, local_name);
    if ((hcdfile_fd = open(fw_path, O_RDONLY)) == -1) {

        p = (char*)(&local_name[0]);
        for (i = 0; (i < LOCAL_NAME_BUFFER_LEN) || (*(p + i) != 0); i++)
            *(p + i) = tolower(*(p + i));

        snprintf(fw_path, BUFFER_SIZE - 1, "%s/%s.hcd", fw_folder_path, local_name);
        if ((hcdfile_fd = open(fw_path, O_RDONLY)) == -1) {
            log2file("File %s could not be opened, error %d\n", fw_path, errno);
            exit(5);
        }
    }

    log2file("Found FW path: %s\n", fw_path);
}

void proc_patchram()
{
    int len;

    log2file("proc_patchram:\n");

    hci_send_cmd(hci_download_minidriver, sizeof(hci_download_minidriver));

    read_event(uart_fd, buffer);

    if (!no2bytes) {
        if (read(uart_fd, &buffer[0], 2) != 2) {
            log2file("Failed to read from uart.\n");
            exit(5);
        }
    }

    if (tosleep) {
        usleep(tosleep);
    }

    while (read(hcdfile_fd, &buffer[1], 3) == 3) {
        buffer[0] = 0x01;

        len = buffer[3];

        if (read(hcdfile_fd, &buffer[4], len) != len) {
            log2file("Failed to read from FW file.\n");
            exit(5);
        }

        hci_send_cmd(buffer, len + 4);
        read_event(uart_fd, buffer);
    }

    if (use_baudrate_for_download) {
        cfsetospeed(&termios, B115200);
        cfsetispeed(&termios, B115200);
        tcsetattr(uart_fd, TCSANOW, &termios);
    }

    proc_reset();
}

void proc_baudrate()
{
    log2file("proc_baudrate:\n");
    hci_send_cmd(hci_update_baud_rate, sizeof(hci_update_baud_rate));
    read_event(uart_fd, buffer);

    cfsetospeed(&termios, termios_baudrate);
    cfsetispeed(&termios, termios_baudrate);
    tcsetattr(uart_fd, TCSANOW, &termios);

    if (debug) {
        log2file("Done setting baudrate\n");
    }
}

void proc_bdaddr()
{
    log2file("proc_bdaddr:\n");
    hci_send_cmd(hci_write_bd_addr, sizeof(hci_write_bd_addr));
    read_event(uart_fd, buffer);
}

void proc_enable_lpm()
{
    log2file("proc_enable_lpm:\n");
    hci_send_cmd(hci_write_sleep_mode, sizeof(hci_write_sleep_mode));
    read_event(uart_fd, buffer);
}

void proc_scopcm()
{
    log2file("proc_scopcm:\n");
    hci_send_cmd(hci_write_sco_pcm_int,
        sizeof(hci_write_sco_pcm_int));

    read_event(uart_fd, buffer);

    hci_send_cmd(hci_write_pcm_data_format,
        sizeof(hci_write_pcm_data_format));

    read_event(uart_fd, buffer);
}

void proc_i2s()
{
    log2file("proc_i2s:\n");
    hci_send_cmd(hci_write_i2spcm_interface_param,
        sizeof(hci_write_i2spcm_interface_param));

    read_event(uart_fd, buffer);
}

void proc_enable_hci()
{
    int i = N_HCI;
    int proto = HCI_UART_H4;

    log2file("proc_enable_hci:\n");

    if (ioctl(uart_fd, TIOCSETD, &i) < 0) {
        log2file("Can't set line discipline\n");
        return;
    }

    if (ioctl(uart_fd, HCIUARTSETPROTO, proto) < 0) {
        log2file("Can't set hci protocol\n");
        return;
    }

    log2file("Done setting line discpline\n");
    return;
}

#ifdef ANDROID
void read_default_bdaddr()
{
    int sz;
    int fd;

    char path[PROPERTY_VALUE_MAX];

    char bdaddr[18];
    int len = 17;
    memset(bdaddr, 0, (len + 1) * sizeof(char));

    property_get("ro.bt.bdaddr_path", path, "");
    if (path[0] == 0)
        return;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        log2file("open(%s) failed: %s (%d)", path, strerror(errno),
            errno);
        return;
    }

    sz = read(fd, bdaddr, len);
    if (sz < 0) {
        log2file("read(%s) failed: %s (%d)", path, strerror(errno),
            errno);
        close(fd);
        return;
    } else if (sz != len) {
        log2file("read(%s) unexpected size %d", path, sz);
        close(fd);
        return;
    }

    if (debug) {
        log2file("Read default bdaddr of %s\n", bdaddr);
    }

    parse_bdaddr(bdaddr);
}
#endif

static void make_daemon()
{
#ifdef ANDROID
    read_default_bdaddr();
#else
    if (isAlreadyRunning() == 1) {
        exit(3);
    }
    daemonize("brcm_patchram_plus");
#endif
}

static void erase_log()
{
    remove(LOG_FILE_NAME);
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        usage("brcm_patchram_plus");
        return (1);
    }

    erase_log();

    make_daemon();

    log2file("### AMPAK FW Auto detection patch version = [%s] ###\n", FW_TABLE_VERSION);

    if (parse_cmd_line(argc, argv)) {
        log2file("#Parse command line failed. rc=%d\n", -1);
        exit(1);
    }

    if (uart_fd < 0) {
        log2file("#UART closed. rc=%d\n", -2);
        exit(2);
    }

    if (init_uart(&termios)) {
        log2file("#UART initialization failed. rc=%d\n", -2);
        exit(2);
    }

    proc_reset();
    proc_read_local_name();
    proc_open_patchram();

    if (use_baudrate_for_download) {
        log2file("#Use baudrate for download. rc=%d\n", 0);
        if (termios_baudrate) {
            proc_baudrate();
        }
    }

    if (hcdfile_fd > 0) {
        log2file("#Patching ram. rc=%d\n", 0);
        proc_patchram();
    }

    if (termios_baudrate) {
        log2file("#Setup baudrate. rc=%d\n", 0);
        proc_baudrate();
    }

    if (bdaddr_flag) {
        log2file("#Setup MAC address. rc=%d\n", 0);
        proc_bdaddr();
    }

    if (enable_lpm) {
        log2file("#Enable LPM. rc=%d\n", 0);
        proc_enable_lpm();
    }

    if (scopcm) {
        log2file("#Enable SCOtoPCM. rc=%d\n", 0);
        proc_scopcm();
    }

    if (i2s) {
        log2file("#Enable I2C. rc=%d\n", 0);
        proc_i2s();
    }

    if (enable_hci) {
        log2file("#Enter HCI loop. rc=%d\n", 0);

        proc_enable_hci();

        while (1) {
            sleep(UINT_MAX);
        }
    }

    exit(0);
}
