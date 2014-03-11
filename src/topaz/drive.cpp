/**
 * File:   $URL $
 * Author: $Author $
 * Date:   $Date $
 * Rev:    $Revision $
 *
 * Topaz - Hard Drive Interface
 *
 * This file implements high level APIs used to communicate with compatible
 * TCG Opal hard drives.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <endian.h>
#include <topaz/defs.h>
#include <topaz/debug.h>
#include <topaz/drive.h>
#include <topaz/exceptions.h>
#include <topaz/uid.h>
using namespace std;
using namespace topaz;

#define PAD_TO_MULTIPLE(val, mult) (((val + (mult - 1)) / mult) * mult)

/**
 * \brief Topaz Hard Drive Constructor
 *
 * @param path OS path to specified drive (eg - '/dev/sdX')
 */
drive::drive(char const *path)
  : raw(path)
{
  // Initialization
  tper_session_id = 0;
  host_session_id = 0;
  has_opal1 = false;
  has_opal2 = false;
  lba_align = 1;
  com_id = 0;
  max_com_pkt_size = -1;
  
  // Check for drive TPM
  probe_tpm();
  
  // Level 0 Discovery tells us about Opal support ...
  probe_level0();
  
  // If we can, make sure we're starting from a blank slate
  if (has_opal2) reset_comid(com_id);
  
  // Query Opal Comm Properties
  probe_level1();
}

/**
 * \brief Topaz Hard Drive Destructor
 */
drive::~drive()
{
  // Cleanup
  logout();
}

/**
 * \brief Combined I/O to TCG Opal drive
 *
 * @param sp_uid Target Security Provider for session (ADMIN_SP / LOCKING_SP)
 */
void drive::login_anon(uint64_t sp_uid)
{
  // If present, end any session in progress
  logout();
  
  // Method Call - SessionMgr.StartSession[]
  datum io;
  io.object_uid() = SESSION_MGR;
  io.method_uid() = START_SESSION;
  
  // Required Arguments (Simple Atoms)
  io[0].value()   = atom::new_uint(1);       // Host Session ID (left at one)
  io[1].value()   = atom::new_uid(sp_uid);   // Admin SP or Locking SP
  io[2].value()   = atom::new_uint(1);       // Read/Write Session
  
  // Off it goes
  sendrecv(io, io);
  
  // Host session ID
  host_session_id = io[0].value().get_uint();
  
  // TPer session ID
  tper_session_id = io[1].value().get_uint();
  
  // Debug
  TOPAZ_DEBUG(1) printf("Anonymous Session %lx:%lx Started\n",
			tper_session_id, host_session_id);
}

/**
 * \brief Combined I/O to TCG Opal drive
 *
 * @param sp_uid Target Security Provider for session (ADMIN_SP / LOCKING_SP)
 * @param user_uid 
 */
void drive::login(uint64_t sp_uid, uint64_t auth_uid, byte_vector pin)
{
  // If present, end any session in progress
  logout();
  
  // Method Call - SessionMgr.StartSession[]
  datum io;
  io.object_uid() = SESSION_MGR;
  io.method_uid() = START_SESSION;
  
  // Required Arguments (Simple Atoms)
  io[0].value()   = atom::new_uint(1);       // Host Session ID (left at one)
  io[1].value()   = atom::new_uid(sp_uid);   // Admin SP or Locking SP
  io[2].value()   = atom::new_uint(1);       // Read/Write Session
  
  // Optional Arguments (Named Atoms)
  io[3].name()        = atom::new_uint(0);       // Host Challenge
  io[3].named_value() = atom::new_bin(pin);
  io[4].name()        = atom::new_uint(3);       // Host Signing Authority (User)
  io[4].named_value() = atom::new_uid(auth_uid);
  
  // Off it goes
  sendrecv(io, io);
  
  // Host session ID
  host_session_id = io[0].value().get_uint();
  
  // TPer session ID
  tper_session_id = io[1].value().get_uint();
  
  // Debug
  TOPAZ_DEBUG(1) printf("Authorized Session %lx:%lx Started\n",
			tper_session_id, host_session_id);
}

/**
 * \brief Query Value from Specified Table
 *
 * @param tbl_uid Identifier of target table
 * @param tbl_col Column number of data to retrieve (table specific)
 * @return Queried parameter
 */
atom drive::table_get(uint64_t tbl_uid, uint64_t tbl_col)
{
  // Method Call - UID.Get[]
  datum io;
  io.object_uid()        = tbl_uid;
  io.method_uid()        = GET;
  io[0][0].name()        = atom::new_uint(3);       // Starting Table Column
  io[0][0].named_value() = atom::new_uint(tbl_col);
  io[0][1].name()        = atom::new_uint(4);       // Ending Tabling Column
  io[0][1].named_value() = atom::new_uint(tbl_col);
  
  // Off it goes
  sendrecv(io, io);
  
  // Return first element of nested array
  return io[0][0].named_value().value();
}

/**
 * \brief Query Value from Specified Table
 *
 * @param tbl_uid Identifier of target table
 * @param tbl_col Column number of data to retrieve (table specific)
 * @param val Value to set in column
 */
void drive::table_set(uint64_t tbl_uid, uint64_t tbl_col, atom val)
{
  // Method Call - UID.Get[]
  datum io;
  io.object_uid()        = tbl_uid;
  io.method_uid()        = SET;
  
  io[0].name()                         = atom::new_uint(1);       // Values
  io[0].named_value()[0].name()        = atom::new_uint(tbl_col);
  io[0].named_value()[0].named_value() = val;
  
  // Off it goes
  sendrecv(io, io);
}

/**
 * \brief Retrieve default device PIN
 */
atom drive::default_pin()
{
  return table_get(C_PIN_MSID, 3);
}

/**
 * \brief Combined I/O to TCG Opal drive
 *
 * @param data Read and write buffer for I/O
 */
void drive::sendrecv(datum &data)
{
  sendrecv(data, data);
}

/**
 * \brief Combined I/O to TCG Opal drive
 *
 * @param data_out Datum to write to drive
 * @param data_in  Datum read from drive
 */
void drive::sendrecv(datum const &data_out, datum &data_in)
{
  // Send the command
  send(data_out);
  
  // Give the drive a moment to work
  usleep(10000); // 10 ms
  
  // Retrieve response
  recv(data_in);
}

/**
 * \brief Send payload to TCG Opal drive
 *
 * @param outbuf Outbound data buffer
 */
void drive::send(datum const &outbuf)
{
  unsigned char *block, *payload;
  opal_header_t *header;
  size_t sub_size, pkt_size, com_size, tot_size;
  
  // Debug
  TOPAZ_DEBUG(3)
  {
    printf("Opal TX: ");
    outbuf.print();
    printf("\n");
  }
  
  // Sub Packet contains the actual data
  sub_size = outbuf.size();
  
  // Packet includes Sub Packet header
  pkt_size = sub_size + sizeof(opal_sub_packet_header_t);
  
  // ... and is also padded to multiple of 4 bytes
  pkt_size = PAD_TO_MULTIPLE(pkt_size, 4);
  
  // Comm Packet includes Packet header
  com_size = pkt_size + sizeof(opal_packet_header_t);
  
  // Grand total includes last header
  tot_size = com_size + sizeof(opal_com_packet_header_t);
  
  // ... and gets padded to multiple of 512 bytes
  tot_size = PAD_TO_MULTIPLE(tot_size, ATA_BLOCK_SIZE);
  
  // Check that the drive can accept this data
  if (tot_size > max_com_pkt_size)
  {
    throw topaz_exception("ComPkt too large for drive");
  }
  
  // Allocate some mem to work with
  block = new unsigned char[tot_size];
  
  // Set up pointers
  header = (opal_header_t*)block;
  payload = block + sizeof(opal_header_t);
  
  // Clear it out
  memset(block, 0, tot_size);
  
  // Fill in headers
  header->com_hdr.com_id = htobe16(com_id);
  header->com_hdr.length = htobe32(com_size);
  header->pkt_hdr.length = htobe32(pkt_size);
  header->sub_hdr.length = htobe32(sub_size);
  
  // Method calls to session manager don't need a session
  if ((outbuf.get_type() == datum::METHOD) &&
      (outbuf.object_uid() == SESSION_MGR))
  {
    // Leave TPer & Host session IDs at 0
  }
  else if (host_session_id == 0) // All others require it ...
  {
    throw topaz_exception("Failed send(): No TPer Session");
  }
  else // Include current TPer & Host session IDs
  {
    header->pkt_hdr.tper_session_id = htobe32(tper_session_id);
    header->pkt_hdr.host_session_id = htobe32(host_session_id);
  }
  
  // Copy over payload data
  outbuf.encode_bytes(payload);
  
  // Hand off formatted Com Packet
  raw.if_send(1, com_id, block, tot_size / ATA_BLOCK_SIZE);
  
  // Cleanup
  delete [] block;
}

/**
 * \brief Receive payload from TCG Opal drive
 *
 * @param inbuf Inbound data buffer
 */
void drive::recv(datum &inbuf)
{
  unsigned char block[ATA_BLOCK_SIZE] = {0}, *payload;
  opal_header_t *header;
  size_t count, min = sizeof(opal_packet_header_t) + sizeof(opal_sub_packet_header_t);
  
  // Set up pointers
  header = (opal_header_t*)block;
  payload = block + sizeof(opal_header_t);
  
  // Receive formatted Com Packet
  raw.if_recv(1, com_id, block, 1);
  
  // Do some cursory verification here
  if (be16toh(header->com_hdr.com_id) != com_id)
  {
    throw topaz_exception("Unexpected ComID in drive response");
  }
  if (be32toh(header->com_hdr.length) <= min)
  {
    throw topaz_exception("Invalid Com Packet length in drive response");
  }
  
  // Ready the receiver buffer
  count = be32toh(header->sub_hdr.length);
  
  // Decode response
  inbuf.decode_bytes(payload, count);
  
  // Debug
  TOPAZ_DEBUG(3)
  {
    printf("Opal RX: ");
    inbuf.print();
    printf("\n");
  }
  
  // If a method call, verify return status
  if ((inbuf.get_type() == datum::METHOD) &&
      (inbuf.status() != datum::STA_SUCCESS))
  {
    throw topaz_exception("Nonzero status on method call (failed)");
  }
}

/**
 * \brief Probe Available TPM Security Protocols
 */
void drive::probe_tpm()
{
  tpm_protos_t protos;
  int i, count;
  bool has_opal = false;
  
  // TPM protocols listed by IF-RECV
  TOPAZ_DEBUG(1) printf("Probe TPM Security Protocols\n");
  raw.if_recv(0, 0, &protos, 1);
  
  // Browse results
  count = be16toh(protos.list_len);
  for (i = 0; i < count; i++)
  {
    int proto = protos.list[i];
    
    // Ultimately, the only one we really need is 0x01
    if (proto == 0x01)
    {
      has_opal = true;
    }
    
    // Though verbose output is also helpful
    TOPAZ_DEBUG(2)
    {
      printf("  (0x%02x) %s\n", proto, lookup_tpm_proto(proto));
    }
  }
  
  // Verify we found 0x01
  if (!has_opal)
  {
    throw topaz_exception("Drive does not support TCG Opal");
  }
}

/**
 * \brief Level 0 Probe - Discovery
 */
void drive::probe_level0()
{
  char data[ATA_BLOCK_SIZE], *feat_data;
  level0_header_t *header = (level0_header_t*)data;
  level0_feat_t *feat;
  uint32_t total_len;
  uint16_t major, minor, code;
  size_t offset = sizeof(level0_header_t);
  
  // Level0 Discovery over IF-RECV
  TOPAZ_DEBUG(1) printf("Establish Level 0 Comms - Discovery\n");
  raw.if_recv(1, 1, &data, 1);
  total_len = 4 + be32toh(header->length);
  major = be16toh(header->major_ver);
  minor = be16toh(header->minor_ver);
  TOPAZ_DEBUG(2)
  {
    printf("  Level0 Size: %d\n",  total_len);
    printf("  Level0 Version: %d / %d\n", major, minor);
  }
  
  // Verify major / minor number of structure
  if ((major != 0) || (minor != 1))
  {
    throw topaz_exception("Unexpected Level0 Revision");
  }
  
  // Tick through returned feature descriptors
  for (offset = sizeof(level0_header_t);
       offset < (total_len - sizeof(level0_feat_t));
       offset += feat->length)
  {
    // Set pointer to feature
    feat = (level0_feat_t*)(data + offset);
    
    // Move to offset of feature data
    offset += sizeof(level0_feat_t);
    feat_data = data + offset;
    
    // Rip it open
    code = be16toh(feat->code);
    TOPAZ_DEBUG(2) printf("  Feature 0x%04x v%d (%d bytes): ", code,
			  feat->version >> 4, feat->length);
    if (code == FEAT_TPER)
    {
      TOPAZ_DEBUG(2)
      {
	printf("Trusted Peripheral (TPer)\n");
	printf("    Sync: %d\n",        0x01 & (data[offset]     ));
	printf("    Async: %d\n",       0x01 & (data[offset] >> 1));
	printf("    Ack/Nak: %d\n",     0x01 & (data[offset] >> 2));
	printf("    Buffer Mgmt: %d\n", 0x01 & (data[offset] >> 3));
	printf("    Streaming: %d\n",   0x01 & (data[offset] >> 4));
	printf("    ComID Mgmt: %d\n",  0x01 & (data[offset] >> 6));
      }
    }
    else if (code == FEAT_LOCK)
    {
      TOPAZ_DEBUG(2)
      {
	printf("Locking\n");
	printf("    Supported: %d\n",        0x01 & (data[offset]     ));
	printf("    Enabled: %d\n",          0x01 & (data[offset] >> 1));
	printf("    Locked: %d\n",           0x01 & (data[offset] >> 2));
	printf("    Media Encryption: %d\n", 0x01 & (data[offset] >> 3));
	printf("    MBR Enabled: %d\n",      0x01 & (data[offset] >> 4));
	printf("    MBR Done: %d\n",         0x01 & (data[offset] >> 5));
      }
    }
    else if (code == FEAT_GEO)
    {
      feat_geo_t *geo = (feat_geo_t*)feat_data;
      lba_align = be64toh(geo->lowest_align);
      TOPAZ_DEBUG(2)
      {
	printf("Geometry Reporting\n");
	printf("    Align Required: %d\n",     0x01 & geo->align); 
	printf("    LBA Size: %d\n",           be32toh(geo->lba_size));
	printf("    Align Granularity: %ld\n", be64toh(geo->align_gran));
	printf("    Lowest Align: %lu\n",      lba_align);
      }
    }
    else if (code == FEAT_OPAL1)
    {
      feat_opal1_t *opal1 = (feat_opal1_t*)feat_data;
      has_opal1 = true;
      lba_align = 1;     // Opal 1.0 doesn't work on large sector drives
      com_id = be16toh(opal1->comid_base);
      TOPAZ_DEBUG(2)
      { 
	printf("Opal SSC 2.0\n");
	printf("    Base ComID: %u\n",            com_id);
	printf("    Number of ComIDs: %d\n",      be16toh(opal1->comid_count));
	printf("    Range cross BHV: %d\n",       0x01 & (opal1->range_bhv));
      }
    }
    else if (code == FEAT_SINGLE)
    {
      TOPAZ_DEBUG(2)
      {
	feat_single_t *single = (feat_single_t*)feat_data;
	printf("Single User Mode\n");
	printf("    Locking Objects Supported: %d\n", be32toh(single->lock_obj_count));
	printf("    Single User Presence: ");
	switch (0x03 & single->bitmask)
	{
	  case 0:
	    printf("None\n");
	    break;
	    
	  case 1:
	    printf("Some\n");
	    break;
	    
	  default:
	    printf("All\n");
	    break;
	}
	printf("    Ownership Policy: %s\n",
	       (0x04 & single->bitmask ? "Admin" : "User"));
      }
    }
    else if (code == FEAT_TABLES)
    {
      TOPAZ_DEBUG(2)
      {
	feat_tables_t *tables = (feat_tables_t*)feat_data;
	printf("Additional DataStore Tables\n");
	printf("    Max Tables: %d\n",     be16toh(tables->max_tables));
	printf("    Max Table Size: %d\n", be32toh(tables->max_size));
	printf("    Table Align: %d\n",    be32toh(tables->table_align));
      }
    }
    else if (code == FEAT_OPAL2)
    {
      feat_opal2_t *opal2 = (feat_opal2_t*)feat_data;
      com_id = be16toh(opal2->comid_base);
      has_opal2 = true;
      admin_count = be16toh(opal2->admin_count);
      user_count = be16toh(opal2->user_count);
      TOPAZ_DEBUG(2)
      { 
	printf("Opal SSC 2.0\n");
	printf("    Base ComID: %u\n",       com_id);
	printf("    Number of ComIDs: %d\n", be16toh(opal2->comid_count));
	printf("    Range cross BHV: %d\n",  0x01 & (opal2->range_bhv));
	printf("    Max SP Admin: %d\n",     admin_count);
	printf("    Max SP User: %d\n",      user_count);
	printf("    C_PIN_SID Initial: ");
	if (opal2->init_pin == 0x00)
	{
	  printf("C_PIN_MSID\n");
	}
	else if (opal2->init_pin == 0xff)
	{
	  printf("Vendor Defined\n");
	}
	else
	{
	  printf("Reserved (%02x)\n", opal2->init_pin);
	}
	printf("    C_PIN_SID Revert: ");
	if (opal2->revert_pin == 0x00)
	{
	  printf("C_PIN_MSID\n");
	}
	else if (opal2->revert_pin == 0xff)
	{
	  printf("Vendor Defined\n");
	}
	else
	{
	  printf("Reserved (%02x)\n", opal2->revert_pin);
	}
      }
    }
    else if ((code >= 0x1000) && (code < 0x4000))
    {
      TOPAZ_DEBUG(2) printf("SSCs");
    }
    else if (code >= 0xc000)
    {
      TOPAZ_DEBUG(2) printf("Vendor Specific\n");
    }
    else
    {
      TOPAZ_DEBUG(2) printf("Reserved\n");
    }
  }
}

/**
 * \brief Level 1 Probe - Host Properties
 */
void drive::probe_level1()
{
  TOPAZ_DEBUG(1) printf("Establish Level 1 Comms - Host Properties\n");
  
  // Gin up a Properties call on Session Manager
  datum io;
  io.object_uid() = SESSION_MGR;
  io.method_uid() = PROPERTIES;
  
  // Query communication properties
  sendrecv(io, io);
  
  // Comm props stored in list (first element) of named items
  datum_vector const &props = io[0].list();
  TOPAZ_DEBUG(2) printf("  Received %lu items\n", props.size());
  
  for (size_t i = 0; i < props.size(); i++)
  {
    // Name of property
    string name = props[i].name().get_string();
    
    // Value
    uint64_t val = props[i].named_value().value().get_uint();
    
    // Only one we want here is the MaxComPacketSize,
    // which specifies the maximum I/O packet length
    if (name == "MaxComPacketSize")
    {
      max_com_pkt_size = val;
      TOPAZ_DEBUG(2) printf("  Max ComPkt Size is %lu (%lu blocks)\n",
			    val, val / ATA_BLOCK_SIZE);
    }
  }
}

/**
 * \brief End a session with drive TPM
 */
void drive::logout()
{
  if (tper_session_id)
  {
    // Debug
    TOPAZ_DEBUG(1) printf("Stopping TPM Session %lx:%lx\n",
			  tper_session_id, host_session_id);
    
    // Gin up an end of session
    datum io(datum::END_SESSION);
    
    // Off it goes
    try
    {
      sendrecv(io);
    }
    catch (topaz_exception &e)
    {
      // Nada
    }
    
    // Mark state
    tper_session_id = 0;
    host_session_id = 0;
  }
}

/**
 * \brief Probe TCG Opal Communication Properties
 */
void drive::reset_comid(uint32_t com_id)
{
  unsigned char block[ATA_BLOCK_SIZE] = {0};
  opal_comid_req_t *cmd = (opal_comid_req_t*)block;
  opal_comid_resp_t *resp = (opal_comid_resp_t*)block;

  // Debug
  TOPAZ_DEBUG(1) printf("Reset ComID 0x%x\n", com_id);

  // Cook up the COMID management packet
  cmd->com_id = htobe16(com_id);
  cmd->req_code = htobe32(0x02);     // STACK_RESET
  
  // Hit the reset
  raw.if_send(2, com_id, block, 1);
  raw.if_recv(2, com_id, block, 1);
  
  // Check result
  if ((htobe32(resp->avail_data) != 4) || (htobe32(resp->failed) != 0))
  {
    throw topaz_exception("Cannot reset ComID");
  }
  
  // Debug
  TOPAZ_DEBUG(2) printf("  Completed\n");
}

/**
 * \brief Convert TPM Protocol ID to String
 *
 * @param proto Protocol ID 0x00 - 0xff
 * @return String representation of ID
 */
char const *drive::lookup_tpm_proto(uint8_t proto)
{
  if (proto == 0)
  {
    return "Security Protocol Discovery";
  }
  else if ((proto >= 1) && (proto <= 6))
  {
    return "TCG Opal";
  }
  else if ((proto == 0x20) || (proto == 0xef))
  {
    return "T10 (Reserved)";
  }
  else if (proto == 0xee)
  {
    return "IEEE P1667";
  }
  else if (proto >= 0xf0)
  {
    return "Vendor Specific";
  }
  else
  {
    return "Reserved";
  }
}
