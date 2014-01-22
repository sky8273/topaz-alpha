#ifndef TOPAZ_DRIVE_H
#define TOPAZ_DRIVE_H

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

#include <topaz/rawdrive.h>
#include <topaz/datum.h>

namespace topaz
{

  class drive
  {
    
  public:
    
    typedef enum
    {
      
    } lifecycle_t;
    
    /**
     * \brief Topaz Hard Drive Constructor
     *
     * @param path OS path to specified drive (eg - '/dev/sdX')
     */
    drive(char const *path);
    
    /**
     * \brief Topaz Hard Drive Destructor
     */
    ~drive();
    
    /**
     * \brief Combined I/O to TCG Opal drive
     *
     * @param sp_uid Target Security Provider for session (ADMIN_SP / LOCKING_SP)
     */
    void login_anon(uint64_t sp_uid);
    
    /**
     * \brief Combined I/O to TCG Opal drive
     *
     * @param sp_uid Target Security Provider for session (ADMIN_SP / LOCKING_SP)
     * @param user_uid 
     */
    void login(uint64_t sp_uid, uint64_t auth_uid, byte_vector pin);
    
    /**
     * \brief Query Value from Specified Table
     *
     * @param tbl_uid Identifier of target table
     * @param tbl_col Column number of data to retrieve (table specific)
     * @return Queried parameter
     */
    atom query_table(uint64_t tbl_uid, uint64_t tbl_col);
    
    /**
     * \brief Retrieve default device PIN
     */
    atom default_pin();
    
    /**
     * \brief Combined I/O to TCG Opal drive
     *
     * @param data Read and write buffer for I/O
     */
    void sendrecv(datum &data);
    
    /**
     * \brief Combined I/O to TCG Opal drive
     *
     * @param data_out Datum to write to drive
     * @param data_in  Datum read from drive
     */
    void sendrecv(datum const &data_out, datum &data_in);
    
    /**
     * \brief Send payload to TCG Opal drive
     *
     * @param outbuf Outbound data buffer
     */
    void send(datum const &outbuf);
    
    /**
     * \brief Receive payload from TCG Opal drive
     *
     * @param inbuf Inbound data buffer
     */
    void recv(datum &inbuf);
    
  protected:
    
    /**
     * \brief Probe Available TPM Security Protocols
     */
    void probe_tpm();
    
    /**
     * \brief Level 0 Probe - Discovery
     */
    void probe_level0();
    
    /**
     * \brief Level 1 Probe - Host Properties
     */
    void probe_level1();

    /**
     * \brief End TPM session
     */
    void logout();
    
    /**
     * \brief Probe TCG Opal Communication Properties
     */
    void reset_comid(uint32_t com_id);
    
    /**
     * \brief Convert TPM Protocol ID to String
     *
     * @param proto Protocol ID 0x00 - 0xff
     * @return String representation of ID
     */
    char const *lookup_tpm_proto(uint8_t proto);
    
    // Underlying Device implementing IF-SEND/RECV
    rawdrive raw;
    
    // TPM session data
    uint64_t tper_session_id;
    uint64_t host_session_id;
    
    // Internal info describing drive
    bool has_opal1;
    bool has_opal2;
    uint32_t com_id;
    uint64_t lba_align;
    uint64_t max_com_pkt_size;
    unsigned admin_count;
    unsigned user_count;
    
  };
  
};

#endif
