/**
 * Topaz - Hard Drive Interface
 *
 * This file implements high level APIs used to communicate with compatible
 * TCG Opal hard drives.
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
#include <cstdio>
#include <cstring>
#include <endian.h>
#include <inttypes.h>
#include <topaz/defs.h>
#include <topaz/debug.h>
#include <topaz/drive.h>
#include <topaz/exceptions.h>
#include <topaz/uid.h>
using namespace std;
using namespace topaz;

#define PAD_TO_MULTIPLE(val, mult) (((val + (mult - 1)) / mult) * mult)

// How often to poll the device for data (millisecs)
#define POLL_MS 10

// How long to wait before timeout thrown
#define TIMEOUT_SECS 5

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
  max_com_pkt_size = 512; // Until otherwise identified
  
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
 * \brief Query max number of admin objects in Locking SP
 */
uint64_t drive::get_max_admins()
{
  return admin_count;
}

/**
 * \brief Query max number of user objects in Locking SP
 */
uint64_t drive::get_max_users()
{
  return user_count;
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
  
  // Parameters - Required Arguments (Simple Atoms)
  datum params;
  params[0].value()   = atom::new_uint(getpid()); // Host Session ID (Process ID)
  params[1].value()   = atom::new_uid(sp_uid);    // Admin SP or Locking SP
  params[2].value()   = atom::new_uint(1);        // Read/Write Session
  
  // Off it goes
  datum rc = invoke(SESSION_MGR, START_SESSION, params);
  
  // Host session ID
  host_session_id = rc[0].value().get_uint();
  
  // TPer session ID
  tper_session_id = rc[1].value().get_uint();
  
  // Debug
  TOPAZ_DEBUG(1) printf("Anonymous Session %" PRIx64 ":%" PRIx64 " Started\n",
			tper_session_id, host_session_id);
}

/**
 * \brief Combined I/O to TCG Opal drive
 *
 * @param sp_uid Target Security Provider for session (ADMIN_SP / LOCKING_SP)
 * @param user_uid 
 */
void drive::login(uint64_t sp_uid, uint64_t auth_uid, string pin)
{
  // If present, end any session in progress
  logout();
  
  // Parameters - Required Arguments (Simple Atoms)
  datum params;
  params[0].value()   = atom::new_uint(getpid()); // Host Session ID (Process ID)
  params[1].value()   = atom::new_uid(sp_uid);    // Admin SP or Locking SP
  params[2].value()   = atom::new_uint(1);        // Read/Write Session
  
  // Optional Arguments (Named Atoms)
  params[3].name()        = atom::new_uint(0);       // Host Challenge
  params[3].named_value() = atom::new_bin(pin.c_str());
  params[4].name()        = atom::new_uint(3);       // Host Signing Authority (User)
  params[4].named_value() = atom::new_uid(auth_uid);
  
  // Off it goes
  datum rc = invoke(SESSION_MGR, START_SESSION, params);
  
  // Host session ID
  host_session_id = rc[0].value().get_uint();
  
  // TPer session ID
  tper_session_id = rc[1].value().get_uint();
  
  // Debug
  TOPAZ_DEBUG(1) printf("Authorized Session %" PRIx64 ":%" PRIx64 " Started\n",
			tper_session_id, host_session_id);
}

/**
 * \brief Query Value from Specified Table
 *
 * @param tbl_uid Identifier of target table
 * @return Queried parameter
 */
datum drive::table_get(uint64_t tbl_uid)
{
  // Parameters - Required Arguments (Simple Atoms)
  datum params;
  params[0] = datum(datum::LIST); // Empty list
  
  // Method Call - UID.Get[]
  datum rc = invoke(tbl_uid, GET, params);
  
  // Return first element of nested array
  return rc[0];
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
  // Parameters - Required Arguments (Simple Atoms)
  datum params;
  params[0][0].name()        = atom::new_uint(3);       // Starting Table Column
  params[0][0].named_value() = atom::new_uint(tbl_col);
  params[0][1].name()        = atom::new_uint(4);       // Ending Tabling Column
  params[0][1].named_value() = atom::new_uint(tbl_col);
  
  // Method Call - UID.Get[]
  datum rc = invoke(tbl_uid, GET, params);
  
  // Return first element of nested array
  return rc[0][0].named_value().value();
}

/**
 * \brief Set Binary Table
 *
 */
void drive::table_set_bin(uint64_t tbl_uid, uint64_t offset,
			  void const *ptr, uint64_t len)
{
  byte const *raw = (byte const *)ptr;
  uint64_t chunk_size, send_size;
  
  // First, estimate how much data we can send with each set call
  chunk_size  = max_com_pkt_size;      // Maximum IF-SEND() size
  chunk_size -= sizeof(opal_header_t); // Header bytes
  chunk_size -= 21;                    // Min size of method call
  chunk_size -= 2 + 1 + 1 + 8;         // First arg, offset (short uint atom)
  chunk_size -= 2 + 1 + 4 + 0;         // Second arg, data (long bin atom)
  chunk_size -= 5;                     // Method status
  chunk_size -= 3;                     // Packet padding (0-3 bytes)
  
  // Biggest multiple of 4096 up to this number
  chunk_size = (chunk_size / 4096) * 4096;
  
  // Send data in one or more chunks
  while (len)
  {
    // Next send is at most chunk_size
    send_size = (len > chunk_size ? chunk_size : len);
    
    // Cook up parameter list for table set
    datum params;
    params[0].name()        = atom::new_uint(0);             // Where
    params[0].named_value() = atom::new_uint(offset);        // Offset of 0
    params[1].name()        = atom::new_uint(1);             // Values
    params[1].named_value() = atom::new_bin(raw, send_size); // Data
    
    // Invoke method
    invoke(tbl_uid, SET, params);
    
    // Bump counters, pointers
    len    -= send_size;
    raw    += send_size;
    offset += send_size;
  }
}

/**
 * \brief Set Value in Specified Table
 *
 * @param tbl_uid Identifier of target table
 * @param tbl_col Column number of data to retrieve (table specific)
 * @param val Value to set in column
 */
void drive::table_set(uint64_t tbl_uid, uint64_t tbl_col, atom val)
{
  // Parameters - Required Arguments (Simple Atoms)
  datum params;
  params[0].name()                         = atom::new_uint(1);       // Values
  params[0].named_value()[0].name()        = atom::new_uint(tbl_col);
  params[0].named_value()[0].named_value() = val;
  
  // Method Call - UID.Set[]
  datum rc = invoke(tbl_uid, SET, params);
}

/**
 * \brief Set Unsigned Value in Specified Table
 *
 * @param tbl_uid Identifier of target table
 * @param tbl_col Column number of data to retrieve (table specific)
 * @param val Value to set in column
 */
void drive::table_set(uint64_t tbl_uid, uint64_t tbl_col, uint64_t val)
{
  // Convenience / clarity wrapper ...
  return table_set(tbl_uid, tbl_col, atom::new_uint(val));
}

/**
 * \brief Retrieve default device PIN
 */
string drive::default_pin()
{
  // MSID PIN is encoded as a byte_vector atom
  byte_vector pin_bytes = table_get(C_PIN_MSID, 3).get_bytes();
  
  // Reroll to string
  string pin;
  for (size_t i = 0; i < pin_bytes.size(); i++)
  {
    pin += pin_bytes[i];
  }
 
  // Completed PIN as string
  return pin;
}

/**
 * \brief Method invocation
 *
 * \param object_uid UID indicating object to use for invocation
 * \param method_uid UID indicating method to call on object
 * \param params Parameters for method call
 * \return Any data returned from method call
 */
datum drive::invoke(uint64_t object_uid, uint64_t method_uid, datum params)
{
  // Set up basic method call
  datum call;
  call.object_uid() = object_uid;
  call.method_uid() = method_uid;
  call.list()       = params.list();
  
  // Debug
  TOPAZ_DEBUG(3)
  {
    printf("Opal Call: ");
    call.print();
    printf("\n");
  }
  
  // Convert to byte vector
  byte_vector bytes = call.encode_vector();
  
  // Tack on method status / control code (TBD - Something cleaner?)
  bytes.push_back(datum::TOK_END_OF_DATA);
  bytes.push_back(datum::TOK_START_LIST);
  bytes.push_back(0); // 0 for execute, some values cancel operations .. (TBD?)
  bytes.push_back(0); // Reserved
  bytes.push_back(0); // Reserved
  bytes.push_back(datum::TOK_END_LIST);
  
  // Send packet to drive.
  // NOTE: Session manager is stateless and doesn't use session ID's ...
  send(bytes, (object_uid != SESSION_MGR));
  
  // Gather response
  recv(bytes);
  
  // Decode response
  datum rc;
  size_t count = rc.decode_vector(bytes);
  
  // Check status code (TBD - Clean this up)
  if (bytes.size() - count != 6)
  {
    throw topaz_exception("Invalid method status on return");
  }
  unsigned status = bytes[count + 2];
  
  // Debug
  TOPAZ_DEBUG(3)
  {
    printf("Opal Return : ");
    rc.print();
    if (status)
    {
      printf(" <STATUS=%u>", status);
    }
    printf("\n");
  }
  
  // Fail out
  if (status)
  {
    throw topaz_exception("Method call failed");
  }
  
  return rc;
}

/**
 * \brief Invoke Revert[] on Admin_SP, and handle session termination
 */
void drive::admin_sp_revert()
{
  // Call it
  invoke(ADMIN_SP, REVERT);
  
  // If this succeeds, the session is terminated immediately
  tper_session_id = 0;
  host_session_id = 0;
}

/**
 * \brief Send payload to TCG Opal drive
 *
 * @param outbuf Outbound data buffer
 * \param session_ids Include TPer session IDs in ComPkt?
 */
void drive::send(byte_vector const &outbuf, bool session_ids)
{
  unsigned char *block, *payload;
  opal_header_t *header;
  size_t sub_size, pkt_size, com_size, tot_size;
  
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
  
  // Include TPer and Host sessions IDs? (All but session manager)
  if (session_ids)
  {
    header->pkt_hdr.tper_session_id = htobe32(tper_session_id);
    header->pkt_hdr.host_session_id = htobe32(host_session_id);
  }
  
  // Copy over payload data
  memcpy(payload, &(outbuf[0]), outbuf.size());
  
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
void drive::recv(byte_vector &inbuf)
{
  unsigned char block[ATA_BLOCK_SIZE] = {0}, *payload;
  opal_header_t *header;
  size_t count;
  byte_vector bytes;
  
  // Maximum poll attempts before timeout
  int max_iters = (TIMEOUT_SECS * 1000) / POLL_MS;
  
  // Set up pointers
  header = (opal_header_t*)block;
  payload = block + sizeof(opal_header_t);
  
  // If still processing, drive may respond with "no data yet" ...
  do
  {
    // Receive formatted Com Packet
    raw.if_recv(1, com_id, block, 1);
    
    // Do some cursory verification here
    if (be16toh(header->com_hdr.com_id) != com_id)
    {
      throw topaz_exception("Unexpected ComID in drive response");
    }
    if (be32toh(header->com_hdr.length) == 0)
    {
      // Response is not yet ready ... wait a bit and try again
      usleep(POLL_MS * 1000);
    }
  } while ((be32toh(header->com_hdr.length) == 0) && (--max_iters > 0));
  
  // Check for timeout
  if (max_iters == 0)
  {
    throw topaz_exception("Timeout waiting for response");
  }
  
  // Ready the receiver buffer
  count = be32toh(header->sub_hdr.length);
  
  // Extract response
  inbuf.resize(count);
  memcpy(&(inbuf[0]), payload, count);
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
	printf("    Align Required: %d\n",    0x01 & geo->align); 
	printf("    LBA Size: %d\n",          be32toh(geo->lba_size));
	printf("    Align Granularity: %u\n", (unsigned int)be64toh(geo->align_gran));
	printf("    Lowest Align: %u\n",      (unsigned int)lba_align);
      }
    }
    else if (code == FEAT_OPAL1)
    {
      feat_opal1_t *opal1 = (feat_opal1_t*)feat_data;
      has_opal1 = true;
      lba_align = 1;     // Opal 1.0 doesn't work on advanced format (4k) drives
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

  // Ask session manager about it's comms properties
  datum rc = invoke(SESSION_MGR, PROPERTIES);
  
  // Comm props stored in list (first element) of named items
  datum_vector const &props = rc[0].list();
  TOPAZ_DEBUG(2) printf("  Received %u items\n", (unsigned int)props.size());
  
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
      TOPAZ_DEBUG(2) printf("  Max ComPkt Size is %" PRIu64 " (%" PRIu64 " blocks)\n",
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
    TOPAZ_DEBUG(1) printf("Stopping TPM Session %" PRIx64 ":%" PRIx64 "\n",
			  tper_session_id, host_session_id);
    
    // Off it goes
    try
    {
      // End of session is a single byte
      byte_vector bytes;
      bytes.push_back(datum::TOK_END_SESSION);
      
      // Bye!
      send(bytes);
      recv(bytes);
    }
    catch (topaz_exception &e)
    {
      // Logout might time out, such as on TPer revert
      // no big deal, and is expected behavior
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
