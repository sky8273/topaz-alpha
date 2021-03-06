/**
 * Topaz - Low Level Hard Drive Interface
 *
 * This file implements low level APIs used to communicate with Linux ATA
 * devices over SCSI translation layer using the SGIO ioctl.
 *
 * Copyright (c) 2014, T Parys
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer. 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/ioctl.h>
#include <scsi/sg.h>
#include <topaz/debug.h>
#include <topaz/defs.h>
#include <topaz/exceptions.h>
#include <topaz/rawdrive.h>
using namespace topaz;

// Set to nonzero to use ATA12 commands
#define USE_ATA12 1

/**
 * \brief Topaz Raw Hard Drive Constructor
 *
 * @param path OS path to specified drive (eg - '/dev/sdX')
 */
rawdrive::rawdrive(char const *path)
{
  // First, verify libata isn't misconfigured ...
  check_libata();
  
  // Open up device
  TOPAZ_DEBUG(1) printf("Opening %s ...\n", path);
  fd = open(path, O_RDWR);
  if (fd == -1)
  {
    throw topaz_exception("Cannot open specified device");
  }
  
  // Check the TPM
  try
  {
    check_tpm();
  }
  catch (topaz_exception &e)
  {
    // Constructor not done, destructor won't be called ...
    close(fd);
    
    // Pass it along
    throw e;
  }
  
}

/**
 * \brief Topaz Raw Hard Drive Destructor
 */
rawdrive::~rawdrive()
{
  // Cleanup
  close(fd);
}

/**
 * if_send (TCG Opal IF-SEND)
 *
 * Low level interface to send data to Drive TPM
 *
 * @param protocol Security Protocol
 * @param comid    Protocol ComId
 * @param data     Data buffer
 * @param bcount   Size of data buffer in 512 byte blocks
 */
void rawdrive::if_send(uint8_t proto, uint16_t comid,
		       void *data, uint8_t bcount)
{
  if (USE_ATA12)
  {
    // ATA12 Command - Trusted Send (0x5e)
    ata12_cmd_t cmd  = {0};
    cmd.feature      = proto;
    cmd.count        = bcount;
    cmd.lba_mid      = comid & 0xff;
    cmd.lba_high     = comid >> 8;
    cmd.command      = 0x5e;
    
    // Off it goes
    ata_exec_12(cmd, SG_DXFER_TO_DEV, data, bcount, 5);
  }
  else
  {
    // ATA16 Command - Trusted Send (0x5e)
    ata16_cmd_t cmd  = {0};
    cmd.feature.low  = proto;
    cmd.count.low    = bcount;
    cmd.lba_mid.low  = comid & 0xff;
    cmd.lba_high.low = comid >> 8;
    cmd.command      = 0x5e;
    
    // Off it goes
    ata_exec_16(cmd, SG_DXFER_TO_DEV, data, bcount, 5);
  }
}

/**
 * if_recv (TCG Opal IF-RECV)
 *
 * Low level interface to receive data from Drive TPM
 *
 * @param protocol Security Protocol
 * @param comid    Protocol ComId
 * @param data     Data buffer
 * @param bcount   Size of data buffer in 512 byte blocks
 */
void rawdrive::if_recv(uint8_t proto, uint16_t comid,
		       void *data, uint8_t bcount)
{
  if (USE_ATA12)
  {
    // ATA12 Command - Trusted Receive (0x5c)
    ata12_cmd_t cmd  = {0};
    cmd.feature      = proto;
    cmd.count        = bcount;
    cmd.lba_mid      = comid & 0xff;
    cmd.lba_high     = comid >> 8;
    cmd.command      = 0x5c;
    
    // Off it goes
    ata_exec_12(cmd, SG_DXFER_FROM_DEV, data, bcount, 5);
  }
  else
  {
    // ATA16 Command - Trusted Receive (0x5c)
    ata16_cmd_t cmd  = {0};
    cmd.feature.low  = proto;
    cmd.count.low    = bcount;
    cmd.lba_mid.low  = comid & 0xff;
    cmd.lba_high.low = comid >> 8;
    cmd.command      = 0x5c;         // Trusted receive
    
    // Off it goes
    ata_exec_16(cmd, SG_DXFER_FROM_DEV, data, bcount, 5);
  }
}

/**
 * check_libata
 *
 * Check libata (Linux ATA layer) for misconfiguration.
 */
void rawdrive::check_libata()
{
  int fd;
  char in;
  
  // Best effort only - /sys may not be mounted
  TOPAZ_DEBUG(1) printf("Probe libata configuration\n");
  fd = open("/sys/module/libata/parameters/allow_tpm", O_RDONLY);
  if (fd != -1)
  {
    // File opened
    if (read(fd, &in, 1) == 1)
    {
      // Data read
      if (in == '0')
      {
	throw topaz_exception(
	  "Linux libata layer configured to block TPM calls (add libata.allow_tpm=1 to kernel args)");
      }
    }
    
    // Cleanup
    close(fd);
  }
}

/**
 * check_tpm
 *
 * Check for presence of Trusted Platform Module (TPM) in drive.
 */
void rawdrive::check_tpm()
{
  uint16_t id_data[256];
  
  // Query identify data
  get_identify(id_data);
  
  // Verify ATA version >= 8
  TOPAZ_DEBUG(1) printf("Verifying ATA support\n");
  if ((id_data[80] & ~((1 < 8) - 1)) == 0)
  {
    throw topaz_exception("ATA device too old to report TPM presence");
  }
  
  // Check for TPM presence
  TOPAZ_DEBUG(1) printf("Searching for TPM Fingerprint\n");
  if ((id_data[48] & 0xC000) != 0x4000)
  {
    throw topaz_exception("No TPM Detected in Specified Drive");
  }
}  

/**
 * ata_identify
 *
 * Retrieve ATA IDENTIFY DEVICE information
 *
 * @param data Data buffer (512 bytes)
 */
void rawdrive::get_identify(uint16_t *data)
{
  if (USE_ATA12)
  {
    // ATA12 Command - Identify Device (0xec)
    ata12_cmd_t cmd = {0};
    cmd.command     = 0xec;
    
    // Off it goes
    TOPAZ_DEBUG(1) printf("Probe ATA Identify\n");
    ata_exec_12(cmd, SG_DXFER_FROM_DEV, data, 1, 1);
  }
  else
  {
    // ATA16 Command - Identify Device (0xec)
    ata16_cmd_t cmd = {0};
    cmd.command     = 0xec;
    
    // Off it goes
    TOPAZ_DEBUG(1) printf("Probe ATA Identify\n");
    ata_exec_16(cmd, SG_DXFER_FROM_DEV, data, 1, 1);
  }
  
  // Useful debug
  TOPAZ_DEBUG(2)
  {
    dump_id_string("Serial", data + 10, 20);
    dump_id_string("Firmware", data + 23, 8);
    dump_id_string("Model", data + 27, 40);
  }
}

/**
 * dump_id_string
 *
 * Print a string encoded in a set of uint16_t data.
 *
 * @param data Pointer to start of uin16_t encoded string
 * @param max  Maximum size of string
 */
void rawdrive::dump_id_string(char const *desc, uint16_t *data, size_t max)
{
  size_t i;
  uint16_t word;
  char c;
  
  printf("  %s: ", desc);
  for (i = 0; i < max; i++)
  {
    word = data[i >> 1];
    
    // Toggle on high/low byte
    if (i % 2)
    {
      c = 0xff & word;
    }
    else
    {
      c = 0xff & (word >> 8);
    }
    
    // Stop on NULL
    if (c == 0x00)
    {
      break;
    }
    // Skip spaces
    if (c != ' ')
    {
      printf("%c", c);
    }
  }
  printf("\n");
}

/**
 * ata_exec_12
 *
 * Execute ATA12 command through SCSI/ATA translation layer,
 * using Linux SGIO ioctl interface.
 *
 * @param cmd    7 byte buffer to valid ATA12 command
 * @param type   IO type (SG_DXFER_NONE/SG_DXFER_FROM_DEV/SG_DXFER_TO_DEV)
 * @param data   Data buffer for ATA operation, NULL on SGIO_DATA_NONE
 * @param bcount Length of data buffer in blocks (512 bytes)
 * @param wait   Command timeout (seconds)
 */
void rawdrive::ata_exec_12(ata12_cmd_t &cmd, int type,
			   void *data, uint8_t bcount, int wait)
{
  struct sg_io_hdr sg_io;  // ioctl data structure
  unsigned char cdb[12];   // Command descriptor block
  unsigned char sense[32]; // SCSI sense (error) data
  int rc;
  
  // Initialize structures
  memset(&sg_io, 0, sizeof(sg_io));
  memset(&cdb, 0, sizeof(cdb));
  memset(&sense, 0, sizeof(sense));
  
  ////
  // Fill in ioctl data for ATA12 pass through
  //
  
  // Mandatory per interface
  sg_io.interface_id    = 'S';
  
  // Location, size of command descriptor block (command)
  sg_io.cmdp            = cdb;
  sg_io.cmd_len         = sizeof(cdb);
  
  // Command data transfer (optional)
  sg_io.dxferp          = data;
  sg_io.dxfer_len       = bcount * ATA_BLOCK_SIZE;
  sg_io.dxfer_direction = type;
  
  // Sense (error) data
  sg_io.sbp             = sense;
  sg_io.mx_sb_len       = sizeof(sense);
  
  // Timeout (ms)
  sg_io.timeout         = wait * 1000;
  
  ////
  // Fill in SCSI command
  //
  
  // Byte 0: ATA12 pass through
  cdb[0] = 0xA1;
  
  // Byte 1: ATA protocol (read/write/none)
  // Byte 2: Check condition, blocks, size, I/O direction
  // Final direction specific bits
  switch (type)
  {
    case SG_DXFER_NONE:
      cdb[1] = 3 << 1; // ATA no data
      cdb[2] = 0x20;   // Check condition only
      break;
      
    case SG_DXFER_FROM_DEV:
      cdb[1] = 4 << 1; // ATA PIO-in
      cdb[2] = 0x2e;   // Check, blocks, size in sector count, read
      break;

    case SG_DXFER_TO_DEV:
      cdb[1] = 5 << 1; // ATA PIO-out
      cdb[2] = 0x26;   // Check, blocks, size in sector count
      break;
      
    default: // Invalid
      throw topaz_exception("Invalid ATA Direction");
      break;
  }
  
  // Rest of ATA12 command get copied here (7 bytes)
  memcpy(cdb + 3, &cmd, 7);
  
  ////
  // Run ioctl
  //
  
  // Debug output command
  TOPAZ_DEBUG(4)
  {
    // Command descriptor block
    printf("ATA Command:\n");
    dump(&cmd, sizeof(cmd));
    
    // Command descriptor block
    printf("SCSI CDB:\n");
    dump(cdb, sizeof(cdb));
    
    // Data out?
    if (type == SG_DXFER_TO_DEV)
    {
      printf("Write Data:\n");
      dump(data, bcount * ATA_BLOCK_SIZE);
    }
  }
  
  // System call
  rc = ioctl(fd, SG_IO, &sg_io);
  if (rc != 0)
  {
    throw topaz_exception("SGIO ioctl failed");
  }
  
  // Debug input
  if (type == SG_DXFER_FROM_DEV)
  {
    TOPAZ_DEBUG(4)
    {
      printf("Read Data:\n");
      dump(data, bcount * ATA_BLOCK_SIZE);
    }
  }
  
  // Check sense data
  if (sense[0] != 0x72 || sense[7] != 0x0e || sense[8] != 0x09
      || sense[9] != 0x0c || sense[10] != 0x00)
  {
    //fprintf(stderr, "error  = %02x\n", sense[11]);    // 0x00 means success
    //fprintf(stderr, "status = %02x\n", sense[21]);    // 0x50 means success
    throw topaz_exception("SGIO ioctl bad status");
  }
}

/**
 * ata_exec_16
 *
 * Execute ATA16 command through SCSI/ATA translation layer,
 * using Linux SGIO ioctl interface.
 *
 * @param cmd    12 byte buffer to valid ATA16 command
 * @param type   IO type (SG_DXFER_NONE/SG_DXFER_FROM_DEV/SG_DXFER_TO_DEV)
 * @param data   Data buffer for ATA operation, NULL on SGIO_DATA_NONE
 * @param bcount Length of data buffer in blocks (512 bytes)
 * @param wait   Command timeout (seconds)
 */
void rawdrive::ata_exec_16(ata16_cmd_t &cmd, int type,
			   void *data, uint8_t bcount, int wait)
{
  struct sg_io_hdr sg_io;  // ioctl data structure
  unsigned char cdb[16];   // Command descriptor block
  unsigned char sense[32]; // SCSI sense (error) data
  int rc;
  
  // Initialize structures
  memset(&sg_io, 0, sizeof(sg_io));
  memset(&cdb, 0, sizeof(cdb));
  memset(&sense, 0, sizeof(sense));
  
  ////
  // Fill in ioctl data for ATA16 pass through
  //
  
  // Mandatory per interface
  sg_io.interface_id    = 'S';
  
  // Location, size of command descriptor block (command)
  sg_io.cmdp            = cdb;
  sg_io.cmd_len         = sizeof(cdb);
  
  // Command data transfer (optional)
  sg_io.dxferp          = data;
  sg_io.dxfer_len       = bcount * ATA_BLOCK_SIZE;
  sg_io.dxfer_direction = type;
  
  // Sense (error) data
  sg_io.sbp             = sense;
  sg_io.mx_sb_len       = sizeof(sense);
  
  // Timeout (ms)
  sg_io.timeout         = wait * 1000;
  
  ////
  // Fill in SCSI command
  //
  
  // Byte 0: ATA16 pass through
  cdb[0] = 0x85;
  
  // Byte 1: ATA protocol (read/write/none)
  // Byte 2: Check condition, blocks, size, I/O direction
  // Final direction specific bits
  switch (type)
  {
    case SG_DXFER_NONE:
      cdb[1] = 3 << 1; // ATA no data
      cdb[2] = 0x20;   // Check condition only
      break;
      
    case SG_DXFER_FROM_DEV:
      cdb[1] = 4 << 1; // ATA PIO-in
      cdb[2] = 0x2e;   // Check, blocks, size in sector count, read
      break;

    case SG_DXFER_TO_DEV:
      cdb[1] = 5 << 1; // ATA PIO-out
      cdb[2] = 0x26;   // Check, blocks, size in sector count
      break;
      
    default: // Invalid
      throw topaz_exception("Invalid ATA Direction");
      break;
  }

  // Rest of ATA16 command get copied here (12 bytes)
  memcpy(cdb + 3, &cmd, 12);
  
  ////
  // Run ioctl
  //
  
  // Debug output command
  TOPAZ_DEBUG(4)
  {
    // Command descriptor block
    printf("ATA Command:\n");
    dump(&cmd, sizeof(cmd));
    
    // Command descriptor block
    printf("SCSI CDB:\n");
    dump(cdb, sizeof(cdb));
    
    // Data out?
    if (type == SG_DXFER_TO_DEV)
    {
      printf("Write Data:\n");
      dump(data, bcount * ATA_BLOCK_SIZE);
    }
  }
  
  // System call
  rc = ioctl(fd, SG_IO, &sg_io);
  if (rc != 0)
  {
    throw topaz_exception("SGIO ioctl failed");
  }
  
  // Debug input
  if (type == SG_DXFER_FROM_DEV)
  {
    TOPAZ_DEBUG(4)
    {
      printf("Read Data:\n");
      dump(data, bcount * ATA_BLOCK_SIZE);
    }
  }
  
  // Check sense data
  if (sense[0] != 0x72 || sense[7] != 0x0e || sense[8] != 0x09
      || sense[9] != 0x0c || sense[10] != 0x00)
  {
    //fprintf(stderr, "error  = %02x\n", sense[11]);    // 0x00 means success
    //fprintf(stderr, "status = %02x\n", sense[21]);    // 0x50 means success
    throw topaz_exception("SGIO ioctl bad status");
  }
}
