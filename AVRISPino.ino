#include <SPI.h>

// November 2011, Christophe Augier <christophe.augier@gmail.com
//
// AVR ISP programmer based on stk500 protocol
//
// Specifications found in:
//  - AVR: In-System Programming
//  - AVR: STK500 Communication protocol
//

#include "pins_arduino.h"

// Signal/Pin mapping

#define RESET SS
#define LED_HB 9
#define LED_ERR 8
#define LED_PMODE 7

// Some macro definitions

#define HW_VER    2
#define SW_MAJOR  1
#define SW_MINOR  18

#define BUFSIZE   128

// Command definitions as of AVR: STK500 Communication Protocol
// (Copied from avrdude stk500.h file)
//

// *****************[ STK Message constants ]***************************

#define STK_SIGN_ON_MESSAGE "AVR STK"   // Sign on string for Cmnd_STK_GET_SIGN_ON

// *****************[ STK Response constants ]***************************

#define Resp_STK_OK                0x10  // ' '
#define Resp_STK_FAILED            0x11  // ' '
#define Resp_STK_UNKNOWN           0x12  // ' '
#define Resp_STK_NODEVICE          0x13  // ' '
#define Resp_STK_INSYNC            0x14  // ' '
#define Resp_STK_NOSYNC            0x15  // ' '

#define Resp_ADC_CHANNEL_ERROR     0x16  // ' '
#define Resp_ADC_MEASURE_OK        0x17  // ' '
#define Resp_PWM_CHANNEL_ERROR     0x18  // ' '
#define Resp_PWM_ADJUST_OK         0x19  // ' '

// *****************[ STK Special constants ]***************************

#define Sync_CRC_EOP               0x20  // 'SPACE'

// *****************[ STK Command constants ]***************************

#define Cmnd_STK_GET_SYNC          0x30  // ' '
#define Cmnd_STK_GET_SIGN_ON       0x31  // ' '

#define Cmnd_STK_SET_PARAMETER     0x40  // ' '
#define Cmnd_STK_GET_PARAMETER     0x41  // ' '
#define Cmnd_STK_SET_DEVICE        0x42  // ' '
#define Cmnd_STK_SET_DEVICE_EXT    0x45  // ' '			

#define Cmnd_STK_ENTER_PROGMODE    0x50  // ' '
#define Cmnd_STK_LEAVE_PROGMODE    0x51  // ' '
#define Cmnd_STK_CHIP_ERASE        0x52  // ' '
#define Cmnd_STK_CHECK_AUTOINC     0x53  // ' '
#define Cmnd_STK_LOAD_ADDRESS      0x55  // ' '
#define Cmnd_STK_UNIVERSAL         0x56  // ' '
#define Cmnd_STK_UNIVERSAL_MULTI   0x57  // ' '

#define Cmnd_STK_PROG_FLASH        0x60  // ' '
#define Cmnd_STK_PROG_DATA         0x61  // ' '
#define Cmnd_STK_PROG_FUSE         0x62  // ' '
#define Cmnd_STK_PROG_LOCK         0x63  // ' '
#define Cmnd_STK_PROG_PAGE         0x64  // ' '
#define Cmnd_STK_PROG_FUSE_EXT     0x65  // ' '		

#define Cmnd_STK_READ_FLASH        0x70  // ' '
#define Cmnd_STK_READ_DATA         0x71  // ' '
#define Cmnd_STK_READ_FUSE         0x72  // ' '
#define Cmnd_STK_READ_LOCK         0x73  // ' '
#define Cmnd_STK_READ_PAGE         0x74  // ' '
#define Cmnd_STK_READ_SIGN         0x75  // ' '
#define Cmnd_STK_READ_OSCCAL       0x76  // ' '
#define Cmnd_STK_READ_FUSE_EXT     0x77  // ' '		
#define Cmnd_STK_READ_OSCCAL_EXT   0x78  // ' '     

// *****************[ STK Parameter constants ]***************************

#define Parm_STK_HW_VER            0x80  // ' ' - R
#define Parm_STK_SW_MAJOR          0x81  // ' ' - R
#define Parm_STK_SW_MINOR          0x82  // ' ' - R
#define Parm_STK_LEDS              0x83  // ' ' - R/W
#define Parm_STK_VTARGET           0x84  // ' ' - R/W
#define Parm_STK_VADJUST           0x85  // ' ' - R/W
#define Parm_STK_OSC_PSCALE        0x86  // ' ' - R/W
#define Parm_STK_OSC_CMATCH        0x87  // ' ' - R/W
#define Parm_STK_RESET_DURATION    0x88  // ' ' - R/W
#define Parm_STK_SCK_DURATION      0x89  // ' ' - R/W

#define Parm_STK_BUFSIZEL          0x90  // ' ' - R/W, Range {0..255}
#define Parm_STK_BUFSIZEH          0x91  // ' ' - R/W, Range {0..255}
#define Parm_STK_DEVICE            0x92  // ' ' - R/W, Range {0..255}
#define Parm_STK_PROGMODE          0x93  // ' ' - 'P' or 'S'
#define Parm_STK_PARAMODE          0x94  // ' ' - TRUE or FALSE
#define Parm_STK_POLLING           0x95  // ' ' - TRUE or FALSE
#define Parm_STK_SELFTIMED         0x96  // ' ' - TRUE or FALSE
#define Param_STK500_TOPCARD_DETECT 0x98  // ' ' - Detect top-card attached

// *****************[ STK status bit definitions ]***************************

#define Stat_STK_INSYNC            0x01  // INSYNC status bit, '1' - INSYNC
#define Stat_STK_PROGMODE          0x02  // Programming mode,  '1' - PROGMODE
#define Stat_STK_STANDALONE        0x04  // Standalone mode,   '1' - SM mode
#define Stat_STK_RESET             0x08  // RESET button,      '1' - Pushed
#define Stat_STK_PROGRAM           0x10  // Program button, '   1' - Pushed
#define Stat_STK_LEDG              0x20  // Green LED status,  '1' - Lit
#define Stat_STK_LEDR              0x40  // Red LED status,    '1' - Lit
#define Stat_STK_LEDBLINK          0x80  // LED blink ON/OFF,  '1' - Blink


boolean in_pmode = false;

void spi_wait()
{
  do {
  } while (!(SPSR & (1 << SPIF)));
}

uint8_t spi_transaction(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
  uint8_t err = 0;

  SPI.transfer(a); 
  uint8_t ret = SPI.transfer(b);

  if (ret != a) {
    err = 1;
  }
  
  // Check if line is still working
  //
  SPI.transfer(c);
  ret = SPI.transfer(d);
  
  if (err != 0) {
    raise_error();
  }
  return ret;
}

void enter_pmode()
{
  if (in_pmode) return;
  
  digitalWrite(LED_PMODE, HIGH);
  in_pmode = true;

  SPI.begin();
  SPI.setBitOrder(MSBFIRST);
  SPI.setDataMode(SPI_MODE0);
  SPI.setClockDivider(SPI_CLOCK_DIV64);

  digitalWrite(SCK, LOW);
  delay(10);
}

void leave_pmode()
{
  SPI.end();
  in_pmode = false;
  digitalWrite(LED_PMODE, LOW);
}

boolean error = false;

void raise_error()
{
  error = true;
  digitalWrite(LED_ERR, HIGH);
  analogWrite(LED_HB, 0);
  delay(2000); // Make the connection break
}

void clear_error()
{
  error = false;
  digitalWrite(LED_ERR, LOW);
} 

// this provides a heartbeat on pin 9, so you can tell the software is running.
//
uint8_t hbval = 128;
int8_t hbdelta = 4;

void heartbeat()
{
  if (error) return;
  if (hbval > 200) hbdelta = -hbdelta;
  if (hbval < 20) hbdelta = -hbdelta;
  hbval += hbdelta;
  analogWrite(LED_HB, hbval);
  delay(20);
}

uint8_t getch()
{
  while(!Serial.available());
  return Serial.read();
}

// Commands implementation
//
boolean check_sync_crc_eop()
{
  if (getch() != Sync_CRC_EOP) {
    raise_error();
    Serial.print((char)Resp_STK_NOSYNC);
    return false;
  }
  return true;
}

void reply_get_sync()
{
  if (!check_sync_crc_eop()) return;

  Serial.print((char)Resp_STK_INSYNC);
  Serial.print((char)Resp_STK_OK);
}

void reply_sign_on()
{
  if (!check_sync_crc_eop()) return;

  Serial.print((char)Resp_STK_INSYNC);
  Serial.print(STK_SIGN_ON_MESSAGE);
  Serial.print((char)Resp_STK_OK);
}

void reply_get_parameter()
{
  uint8_t param = getch();

  if (!check_sync_crc_eop()) return;

  Serial.print((char)Resp_STK_INSYNC);

  uint8_t res = 0;
  switch(param) {
    case Parm_STK_HW_VER:
      res = HW_VER;
      break;
    case Parm_STK_SW_MAJOR:
      res = SW_MAJOR;
      break;
    case Parm_STK_SW_MINOR:
      res = SW_MINOR;
      break;
    case Parm_STK_VTARGET:
      res = 50;
      break;
    case Parm_STK_BUFSIZEH:
      res = (BUFSIZE >> 8) & 0xFF;
      break;
    case Parm_STK_BUFSIZEL:
      res = BUFSIZE & 0xFF;
      break;

    default:
      Serial.print((char)param);
      Serial.print((char)Resp_STK_FAILED);
      return;   
  }
  Serial.print((char)res);
  Serial.print((char)Resp_STK_OK);
}

struct {
  uint8_t devicecode;
  uint8_t revision;
  uint8_t progtype;
  uint8_t parmode;
  uint8_t polling;
  uint8_t selftimed;
  uint8_t lockbytes;
  uint8_t fusebytes;
  uint8_t flashpollval1;
  uint8_t flashpollval2;
  uint8_t eeprompollval1;
  uint8_t eeprompollval2;
  uint8_t pagesizehigh;
  uint8_t pagesizelow;
  uint8_t eepromsizehigh;
  uint8_t eepromsizelow;
  uint8_t flashsize4;
  uint8_t flashsize3;
  uint8_t flashsize2;
  uint8_t flashsize1;
  
  // extended parameters
  uint8_t commandsize;
  uint8_t eeprompagesize;
  uint8_t signalpagel;
  uint8_t signalbs2;
  uint8_t resetdisable;
} dev_params;

boolean device_is_set = false;

void reply_set_device()
{
  dev_params.devicecode = getch();
  dev_params.revision = getch();
  dev_params.progtype = getch();
  dev_params.parmode = getch();
  dev_params.polling = getch();
  dev_params.selftimed = getch();
  dev_params.lockbytes = getch();
  dev_params.fusebytes = getch();
  dev_params.flashpollval1 = getch();
  dev_params.flashpollval2 = getch();
  dev_params.eeprompollval1 = getch();
  dev_params.eeprompollval2 = getch();
  dev_params.pagesizehigh = getch();
  dev_params.pagesizelow = getch();
  dev_params.eepromsizehigh = getch();
  dev_params.eepromsizelow = getch();
  dev_params.flashsize4 = getch();
  dev_params.flashsize3 = getch();
  dev_params.flashsize2 = getch();
  dev_params.flashsize1 = getch();

  if (!check_sync_crc_eop()) return;

  device_is_set = true;

  Serial.print((char)Resp_STK_INSYNC);
  Serial.print((char)Resp_STK_OK);
}

void reply_set_device_ext()
{
  dev_params.commandsize = getch();
  dev_params.eeprompagesize = getch();
  dev_params.signalpagel = getch();
  dev_params.signalbs2 = getch();
  dev_params.resetdisable = getch();

  if (!check_sync_crc_eop()) return;

  Serial.print((char)Resp_STK_INSYNC);
  Serial.print((char)Resp_STK_OK);
}

void reply_enter_progmode()
{
  if (!check_sync_crc_eop()) return;

  Serial.print((char)Resp_STK_INSYNC);

  if (device_is_set) {

    enter_pmode();
      
    // Send a pulse of at least two cycles on RESET
    //
    digitalWrite(RESET, HIGH);
    delay(1);
    
    // Set RESET and SCK to low for at least 20ms
    //
    digitalWrite(RESET, LOW);
    delay(30);
    
    SPI.transfer(0xAC);
    SPI.transfer(0x53);
    uint8_t ret = SPI.transfer(0x00);
    SPI.transfer(0x00);

    if (ret != 0x53) {
      raise_error();
      Serial.print((char)Resp_STK_NODEVICE);
      return;
    }
    Serial.print((char)Resp_STK_OK);
  } else {
    Serial.print((char)Resp_STK_NODEVICE);
  }
}

void reply_leave_progmode()
{
  if (!check_sync_crc_eop()) return;

  leave_pmode();

  Serial.print((char)Resp_STK_INSYNC);
  Serial.print((char)Resp_STK_OK);
}

void reply_universal()
{
  uint8_t b1 = getch(); 
  uint8_t b2 = getch();
  uint8_t b3 = getch();
  uint8_t b4 = getch();

  if (!check_sync_crc_eop()) return;

  uint8_t ret = 0;
  if (b1 != 0xa0) {
    ret = spi_transaction(b1, b2, b3, b4);
  }

  Serial.print((char)Resp_STK_INSYNC);
  Serial.print((char)ret);
  Serial.print((char)Resp_STK_OK);

  // Detect a chip erase and wait for at least tWD_erase
  //
  if (b1 == 0xAC && (b2 & 0x80) == 0x80) {
    delay(10);
  }
}

void reply_chip_erase()
{
  if (!check_sync_crc_eop()) return;

  spi_transaction(0xAC, 0x80, 0x00, 0x00);
  delay(10);

  Serial.print((char)Resp_STK_INSYNC);
  Serial.print((char)Resp_STK_OK);
}

uint16_t cur_addr;

void reply_load_address()
{
  cur_addr = getch();
  cur_addr |= ((uint16_t) getch()) << 8;
  
  if (!check_sync_crc_eop()) return;

  // FIXME: Added load extended address to 0x00, is it needed?
  //
  //spi_transaction(0x4D, 0x00, 0x00, 0x00);

  Serial.print((char)Resp_STK_INSYNC);
  Serial.print((char)Resp_STK_OK);
}


uint16_t get_address_page(uint16_t addr)
{
  uint16_t mask = ~(dev_params.pagesizelow - 1);
  return (addr & mask) >> 1;
}

static uint8_t buf[BUFSIZE];

void wait_for_rdy()
{
  while(spi_transaction(0xf0, 0x00, 0x00, 0x00) != 0); 
}

// length need to be <= BUFSIZE
//
void write_flash(uint16_t addr, uint8_t length)
{
  uint16_t page = get_address_page(addr);
  uint8_t  i = 0;

  while (i < length) {
    // Load low and high bytes at addr
    //
    spi_transaction(0x40, (addr >> 8) & 0xFF, addr & 0xFF, buf[i++]);
    spi_transaction(0x48, (addr >> 8) & 0xFF, addr & 0xFF, buf[i++]);
   
    addr++;

    // If we just loaded the last couple of bytes on the page,
    // exec writing page.
    //
    uint16_t cur_page = get_address_page(addr);
    if (page != cur_page) {
      spi_transaction(0x4C, (page >> 8) & 0xFF, page & 0xFF, 0x00);
      delay(15);
      page = cur_page;
    }
  }
  delay(15);
}

boolean first_time = true;

void reply_prog_page()
{
  uint16_t size = ((uint16_t)getch()) << 8 | getch();

  // Read memory type: E for EEPROM, F for FLASH
  //
  uint8_t mem_type = getch();

  if (size > BUFSIZE) {
    raise_error();
    Serial.print((char)Resp_STK_NOSYNC);
    return;
  }

  // BUFSIZE is < 256 so continue using uin8_t
  //
  uint8_t size_l = size & 0xFF;

  for (uint8_t i = 0; i < size_l; i++) {
    buf[i] = 0;
  }

  for (uint8_t i = 0; i < size_l; i++) {
    uint8_t c = getch();
    //buf[i] = (cur_addr & 0xFF)+ i;
    if (first_time) buf[i] = c;
  }

  first_time = false;

  if (!check_sync_crc_eop()) return;

  if (mem_type == 'F') {
    write_flash(cur_addr, size_l);
  }

  Serial.print((char)Resp_STK_INSYNC);
  Serial.print((char)Resp_STK_OK);
}

void read_flash(uint16_t addr, uint8_t length)
{
  uint8_t i = 0;

  while (i < length) {
    uint8_t addr_h = (addr >> 8) & 0xFF;
    uint8_t addr_l = addr & 0xFF;

    // Read low and high bytes at addr
    //
    buf[i++]   = spi_transaction(0x20, addr_h, addr_l, 0x00);
    buf[i++] = spi_transaction(0x28, addr_h, addr_l, 0x00);

    addr++;
  }
}

void reply_read_page()
{
  uint16_t size = ((uint16_t)getch()) << 8 | getch();
 
  // Read memory type: E for EEPROM, F for FLASH
  //
  uint8_t mem_type = getch();

  // Note: again we don't care for more than BUFSIZE bytes buffers
  if (size > BUFSIZE) {
    raise_error();
    Serial.print((char)Resp_STK_NOSYNC);
    return;
  }
  
  if (!check_sync_crc_eop()) return;

  Serial.print((char)Resp_STK_INSYNC);

  // BUFSIZE is < 256 so continue using uin8_t
  //
  uint8_t size_l = size & 0xFF;

  if (mem_type == 'F') {
    read_flash(cur_addr, size_l);
  }

  for (uint8_t i = 0; i < size_l; i++) {
    Serial.print((char)buf[i]);
  }

  Serial.print((char)Resp_STK_OK);
}

void read_command()
{
  uint8_t cmd = getch();

  switch(cmd) {
    case Cmnd_STK_GET_SYNC:
      reply_get_sync();
      break;
    case Cmnd_STK_GET_SIGN_ON:
      reply_sign_on();
      break;
    case Cmnd_STK_GET_PARAMETER:
      reply_get_parameter();
      break;
    case Cmnd_STK_SET_DEVICE:
      reply_set_device();
      break;
    case Cmnd_STK_SET_DEVICE_EXT:
      reply_set_device_ext();
      break;
    case Cmnd_STK_ENTER_PROGMODE:
      reply_enter_progmode();
      break;
    case Cmnd_STK_LEAVE_PROGMODE:
      reply_leave_progmode();
      break;
    case Cmnd_STK_UNIVERSAL:
      reply_universal();
      break;
    case Cmnd_STK_CHIP_ERASE:
      reply_chip_erase();
      break;
    case Cmnd_STK_LOAD_ADDRESS:
      reply_load_address();
      break;
    case Cmnd_STK_PROG_PAGE:
      reply_prog_page();
      break;
    case Cmnd_STK_READ_PAGE:
      reply_read_page();
      break;


    default:
      if (check_sync_crc_eop()) {
        Serial.print((char)Resp_STK_UNKNOWN);
      }
      break;
  }
}


void setup() {
  Serial.begin(19200);
  pinMode(LED_PMODE, OUTPUT);
  digitalWrite(LED_PMODE, LOW);
  pinMode(LED_ERR, OUTPUT);
  digitalWrite(LED_ERR, LOW);
  pinMode(LED_HB, OUTPUT);
}

void loop(void)
{
  heartbeat();

  if (Serial.available() == 0) {
    return;
  }

  read_command();
}
