#ifndef TOPAZ_DATUM_H
#define TOPAZ_DATUM_H

/**
 * File:   $URL $
 * Author: $Author $
 * Date:   $Date $
 * Rev:    $Revision $
 *
 * Topaz - Datum
 *
 * This class implements a TCG Opal Data Item, that is a higher level, possibly
 * aggregate data type. This includes basic atom types (integers and binary),
 * but also more complex aggregate types such as named types (key/value), lists,
 * and method calls.
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

#include <topaz/atom.h>
#include <topaz/encodable.h>

namespace topaz
{
  
  class datum : public encodable
  {
    
  public:
    
    // Enumeration of various datum types
    typedef enum
    {
      UNSET,      // Not yet set
      ATOM,       // Binary or integer type
      NAMED,      // Key / Value pair
      LIST,       // List (possibly empty)
      METHOD,     // Method call
      END_SESSION // End of session indicator
    } type_t;
    
    // Enumerations of data stream tokens
    typedef enum
    {
      // Sequence Tokens
      TOK_START_LIST  = 0xf0,
      TOK_END_LIST    = 0xf1,
      TOK_START_NAME  = 0xf2,
      TOK_END_NAME    = 0xf3,
      
      // Control Tokens
      TOK_CALL        = 0xf8,
      TOK_END_OF_DATA = 0xf9,
      TOK_END_SESSION = 0xfa,
      TOK_START_TRANS = 0xfb,
      TOK_END_TRANS   = 0xfc,
    } token_t;
    
    // Enumerations of method status ID's
    typedef enum
    {
      STA_SUCCESS               = 0x00,
      STA_NOT_AUTHORIZED        = 0x01,
      STA_OBSOLETE              = 0x02,
      STA_SP_BUSY               = 0x03,
      STA_SP_FAILED             = 0x04,
      STA_SP_DISABLED           = 0x05,
      STA_SP_FROZEN             = 0x06,
      STA_NO_SESSIONS_AVAILABLE = 0x07,
      STA_UNIQUENESS_CONFLICT   = 0x08,
      STA_INSUFFICIENT_SPACE    = 0x09,
      STA_INSUFFICIENT_ROWS     = 0x0A,
      STA_INVALID_PARAMETER     = 0x0C,
      STA_OBSOLETE2             = 0x0D,
      STA_OBSOLETE3             = 0x0E,
      STA_TPER_MALFUNCTION      = 0x0F,
      STA_TRANSACTION_FAILURE   = 0x10,
      STA_RESPONSE_OVERFLOW     = 0x11,
      STA_AUTHORITY_LOCKED_OUT  = 0x12
    } status_t;
    
    /**
     * \brief Default Constructor
     */
    datum();
    
    /**
     * \brief Token Constructor
     */
    datum(datum::type_t data_type);
    
    /**
     * \brief Destructor
     */
    ~datum();
    
    /**
     * \brief Query encoded size
     *
     * @return Byte count of object when encoded
     */
    virtual size_t size() const;
    
    /**
     * \brief Encode to data buffer
     *
     * @param data Data buffer of at least size() bytes
     * @return Number of bytes encoded
     */
    virtual size_t encode_bytes(byte *data) const;
    
    /**
     * \brief Decode from data buffer
     *
     * @param data Location to read encoded bytes
     * @param len  Length of buffer
     * @return Number of bytes processed
     */
    virtual size_t decode_bytes(byte const *data, size_t len);
    
    /**
     * \brief Query Datum Type
     */
    datum::type_t get_type() const;

    /**
     * \brief Query Name
     */
    atom &name();
    
    /**
     * \brief Query Name (const)
     */
    atom const &name() const;
    
    /**
     * \brief Query Value
     */
    atom &value();
    
    /**
     * \brief Query Value (const)
     */
    atom const &value() const;
    
    /**
     * \brief Query Method's Object UID
     */
    uint64_t &object_uid();
    
    /**
     * \brief Query Method's Object UID (const)
     */
    uint64_t const &object_uid() const;
    
    /**
     * \brief Query Value Method UID
     */
    uint64_t &method_uid();
    
    /**
     * \brief Query Value Method UID (const)
     */
    uint64_t const &method_uid() const;
    
    /**
     * \brief Query Value Method UID
     */
    status_t &status();
    
    /**
     * \brief Query Value Method UID (const)
     */
    status_t const &status() const;
    
    /**
     * \brief Query List
     */
    datum_vector &list();
    
    /**
     * \brief Query List (const)
     */
    datum_vector const &list() const;
    
    /**
     * \brief Equality Operator
     *
     * @return True when equal
     */
    bool operator==(datum const &ref);
    
    /**
     * \brief Inequality Operator
     *
     * @return True when not equal
     */
    bool operator!=(datum const &ref);
    
    /**
     * \brief Datum array access
     *
     * @param idx Item to return
     * @return Specified item
     */
    datum &operator[](size_t idx);
    
    /**
     * \brief Debug print
     */
    virtual void print() const;
    
  protected:
    
    /**
     * \brief Ensure data to be decoded is of minimum size
     *
     * @param len Length of buffer
     * @param min Minimum size required for buffer
     */
    void decode_check_size(size_t len, size_t min) const;
    
    /**
     * \brief Verify next token exists and is expected value
     *
     * @param data Location to read encoded bytes
     * @param len  Length of buffer
     * @param idx  Next index
     * @param next Expected value at index
     */
    void decode_check_token(byte const *data, size_t len, size_t idx, byte next) const;
    
    // What sort of object
    datum::type_t data_type;
    
    // Atom storage
    atom data_name;           // Valid only on named object
    atom data_value;
    
    // Method call specific parameters
    uint64_t data_object_uid; // Object reference
    uint64_t data_method_uid; // Method reference
    status_t data_status;     // Method call status code
    
    // Storage of other datums (lists / method calls)
    datum_vector data_list;
    
  };
  
};

#endif
