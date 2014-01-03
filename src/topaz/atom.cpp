/**
 * File:   $URL $
 * Author: $Author $
 * Date:   $Date $
 * Rev:    $Revision $
 *
 * Topaz - Atom
 *
 * This class implements a TCG Opal Atom, that is a base data type which
 * encompasses both integer and binary data types, but does NOT include other
 * data stream tokens such as those for named types, lists, or method calls.
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

#include <cstring>
#include <endian.h>
#include <topaz/atom.h>
#include <topaz/exceptions.h>
using namespace topaz;

/**
 * \brief Default Constructor
 */
atom::atom()
{
  // Initialize
  data_type = atom::EMPTY;
  data_enc = atom::NONE;
  int_skip = 0;
  uint_val = 0;
}

/**
 * \brief Unsigned Integer Constructor
 *
 * @param value Unsigned Integer to represent
 * @param Value is Unique ID (integer encoded as binary)
 */
atom::atom(uint64_t value, bool is_uid)
{
  union
  {
    uint64_t flip;
    byte     raw[8];
  };
  
  // Initialize
  data_type = atom::UINT;
  uint_val = value;
  
  // Unique ID's (UIDs) are quirky. They are 64 bit integers, but get
  // encoded like a byte sequence, of a single length 8 (short).
  // This is simultaneously simpler, and infuriating ...
  if (is_uid)
  {
    // Stored as binary
    data_type = atom::BYTES;
    pick_encoding(8);
    
    // Flip integer big endian
    flip = htobe64(value);
    
    // Now binary
    bytes.resize(8);
    for (size_t i = 0; i < 8; i++)
    {
      bytes[i] = raw[i];
    }
  }
  // Really small values fit into a single byte
  else if (value < 0x40)
  {
    // Tiny atom (Data in same byte as header)
    data_enc = atom::TINY;
    int_skip = 0;
  }
  else // Determine how many bytes are really needed
  {
    // Byteflip to big endian
    flip = htobe64(value);
    
    // Drop unneeded leading zeroes (up to 7)
    for (int_skip = 0; (int_skip < 8) && (raw[int_skip] == 0x00); int_skip++) {}
    
    // All integers less than 16 bytes long (128 bits) will fit in this ...
    data_enc = atom::SHORT;
  }
}

/**
 * \brief Integer Constructor
 *
 * @param value Integer to represent
 */
atom::atom(int64_t value)
{
  union
  {
    uint64_t flip;
    byte     raw[8];
  };
  
  // Initialize
  data_type = atom::INT;
  int_val = value;
  
  // Really small values fit into a single byte
  if ((value < 0x20) && (value >= -0x20))
  {
    // Tiny atom (Data in same byte as header)
    data_enc = atom::TINY;
    int_skip = 0;
  }
  else // Determine how many bytes are really needed
  {
    // Byteflip to big endian
    flip = htobe64(value);
    
    // Logic differs a bit based on sign
    if (value < 0)
    {
      // Negative condition is to drop 0xff bytes,
      // provided the remaining value is still negative
      // (most significant remaining bit is a 1)
      for (int_skip = 0; (int_skip < 8) && (raw[int_skip] == 0xff) &&
	     ((raw[int_skip + 1] & 0x80) == 0x80); int_skip++) {}
    }
    else
    {
      // Positive condition is to drop 0x00 bytes,
      // provided the remaining value is still positive
      // (most significant remaining bit is a 0)
      for (int_skip = 0; (int_skip < 8) && (raw[int_skip] == 0x00) &&
	     ((raw[int_skip + 1] & 0x80) == 0x00); int_skip++) {}
    }
    
    // All integers less than 16 bytes long (128 bits) will fit in this ...
    data_enc = atom::SHORT;
  }
}

/**
 * \brief Binary (Bytes) Constructor
 *
 * @param data Buffer to represent
 * @param len  Length of buffer
 */
atom::atom(byte const *data, size_t len)
{
  // Intialize
  data_type = atom::BYTES;
  
  // Pick data encoding
  pick_encoding(len);
  
  // Copy data over
  bytes.resize(len);
  for (size_t i = 0; i < len; i++)
  {
    bytes[i] = data[i];
  }
}

/**
 * \brief Binary (Bytes) Constructor
 *
 * @param data Container to represent
 */
atom::atom(byte_vector data)
{
  // Intialize
  data_type = atom::BYTES;
  
  // Pick data encoding
  pick_encoding(data.size());
  
  // Copy data over
  bytes = data;
}

/**
 * \brief Destructor
 */
atom::~atom()
{
  // Nada
}

/**
 * \brief Equality Operator
 *
 * @return True when equal
 */
bool atom::operator==(atom const &ref)
{
  // Check to make sure both are same type and encoding
  if ((data_type == ref.data_type) && (data_enc == ref.data_enc))
  {
    // Type specific checks
    switch (data_type)
    {
      case atom::UINT:
      case atom::INT:
	// Works the same for both
	if (uint_val == ref.uint_val)
	{
	  return true;
	}
	break;
	
      case atom::BYTES:
	// Compare size and bytes
	if (bytes.size() == ref.bytes.size())
	{
	  // Check each byte
	  for (size_t i = 0; i < bytes.size(); i++)
	  {
	    if (bytes[i] != ref.bytes[i])
	    {
	      return false;
	    }
	  }
	  
	  // All bytes match 
	  return true;
	}
	break;
	
      default:
	// Misc type, nothing further to check
	return true;
	break;
    }
  }
  
  // Match fail
  return false;
}

/**
 * \brief Inequality Operator
 *
 * @return True when not equal
 */
bool atom::operator!=(atom const &ref)
{
  return !((*this) == ref);
}

/**
 * \brief Query encoded size
 *
 * @return Byte count of object when encoded
 */
size_t atom::size() const
{
  // Empty and Tiny atoms are one byte
  if ((data_type == atom::EMPTY) || (data_enc == atom::TINY))
  {
    return 1;
  }
  else if (data_type == atom::BYTES)
  {
    // Binary data
    return get_header_size() + bytes.size();
  }
  else
  {
    // Signed / Unsigned Integer
    return get_header_size() + (8 - int_skip);
  }
}

/**
 * \brief Encode to data buffer
 *
 * @param data Data buffer of at least size() bytes
 * @return Number of bytes encoded
 */
size_t atom::encode_bytes(byte *data) const
{
  size_t len, i = 0;
  union
  {
    uint64_t flip;
    byte     raw[8];
  };
  byte const *enc_data;

  // Figure out WHAT we're encoding
  switch (data_type)
  {
    case atom::EMPTY:
      // Trivial case - Empty atom, single byte, done
      data[i++] = atom::EMPTY_TOK;
      return i;
      
    case atom::UINT:
    case atom::INT:
      // Integers
      len = 8 - int_skip;         // How many bytes getting stored
      flip = htobe64(uint_val);   // Big endian order
      enc_data = raw + int_skip;  // Pointer to starting byte
      break;
      
    default: // atom::BYTES:
      // Binary data
      len = bytes.size();         // Length from container
      enc_data = &(bytes[0]);     // Pointer to starting byte
      break;
  }
  
  // Write out the atom header
  switch (data_enc)
  {
    case atom::TINY:
      // Tiny atom - Header and Data in one byte
      
      // First byte
      data[i] = atom::TINY_TOK;
      if (data_type == atom::INT) data[i] |= atom::TINY_SIGN;
      data[i++] |= 0x3f & uint_val;
      
      // Nothing further to do
      return i;
      
    case atom::SHORT:
      // Short atom - 1 byte header, <16 byte data
      
      // First byte
      data[i] = atom::SHORT_TOK;
      if (data_type == atom::BYTES) data[i] |= atom::SHORT_BIN;
      if (data_type == atom::INT) data[i] |= atom::SHORT_SIGN;
      data[i++] |= len;
      
      break;
      
    case atom::MEDIUM:
      // Medium atom - 2 byte header, <2048 byte data
      
      // First byte
      data[i] = atom::MEDIUM_TOK;
      if (data_type == atom::BYTES) data[i] |= atom::MEDIUM_BIN;
      if (data_type == atom::INT) data[i] |= atom::MEDIUM_SIGN;
      data[i++] |= len >> 8;
      
      // Second byte
      data[i++] = 0xff & len;
      
      break;
      
    default: // atom::LONG:
      // Long atom - 4 byte header, <16777216 byte data
      
      // First byte
      data[i] = atom::LONG_TOK;
      if (data_type == atom::BYTES) data[i] |= atom::LONG_BIN;
      if (data_type == atom::INT) data[i] |= atom::LONG_SIGN;
      i++;
      
      // Second byte
      data[i++] = 0xff & (len >> 16);
      
      // Third byte
      data[i++] = 0xff & (len >> 8);
      
      // Fourth byte
      data[i++] = 0xff & len;
      
      break;
  }
  
  // Finally, copy in the atom's payload
  memcpy(data + i, enc_data, len);
  
  // Final byte count
  return i + len;
}

/**
 * \brief Decode from data buffer
 *
 * @param data Location to read encoded bytes
 * @param len  Length of buffer
 * @return Number of bytes processed
 */
size_t atom::decode_bytes(byte const *data, size_t len)
{
  size_t head_bytes = 0, count = 0;
  
  // Minimum 1 byte
  decode_check_size(len, 1);
  
  // What is it?
  if (data[0] == atom::EMPTY_TOK)
  {
    // Empty Atom (no data)
    data_type = atom::EMPTY;
    data_enc = atom::NONE;
    
    // No further processing
    return 1;
  }
  if (data[0] < atom::SHORT_TOK)
  {
    // Tiny Atom (Data stored in header)
    data_enc = atom::TINY;
    
    // Determine type
    decode_set_type(0x03 & (data[0] >> 6));
    
    // Must be integer (Note: union type)
    int_val = 0x3f & data[0];
    if ((data_type == atom::INT) && (data[0] & 0x20))
    {
      // Negative signed integer - Sign extend
      int_val |= ~(0x3fULL);
    }
    
    // No further processing
    return 1;
  }
  else if (data[0] < atom::MEDIUM_TOK)
  {
    // Short Atom (1 byte header)
    data_enc = atom::SHORT;
    head_bytes = 1;
    
    // Determine type
    decode_set_type(0x03 & (data[0] >> 4));
    
    // Determine size
    count = data[0] & 0x0f;
  }
  else if (data[0] < atom::LONG_TOK)
  {
    // Medium Atom (2 byte header)
    data_enc = atom::MEDIUM;
    head_bytes = 2;
    decode_check_size(len, head_bytes);
    
    // Determine type
    decode_set_type(0x03 & (data[0] >> 3));
    
    // Determine size
    count = 0x07 & data[0];
    count = (count << 8) + data[1];
  }
  else if (data[0] < 0xe4)
  {
    // Long Atom (4 byte header)
    data_enc = atom::LONG;
    head_bytes = 4;
    decode_check_size(len, head_bytes);
    
    // Determine type
    decode_set_type(0x03 & data[0]);
    
    // Determine size
    count = data[1];
    count = (count << 8) + data[2];
    count = (count << 8) + data[3];
  }
  else // Reserved, or non-atom token (0xe4 - 0xfe)
  {
    throw topaz::exception("Cannot parse atom (invalid token)");
  }
  
  // Ensure expected remaining data is present
  decode_check_size(len, head_bytes + count);
  
  // Load atom payload
  if ((data_type == atom::UINT) || (data_type == atom::INT))
  {
    // Parse integers
    decode_int(data + head_bytes, count);
  }
  else if (data_type == atom::BYTES)
  {
    // Binary data
    bytes.resize(count);
    for (size_t i = 0; i < count; i++)
    {
      bytes[i] = data[head_bytes + i];
    }
  }
  
  // Final size
  return head_bytes + count;
}

/**
 * \brief Query Atom Type
 *
 * @return Type of atom
 */
atom::type_t atom::get_type() const
{
  return data_type;
}

/**
 * \brief Query Atom Encoding
 *
 * @return Encoding of atom
 */
atom::enc_t atom::get_enc() const
{
  return data_enc;
}

/**
 * \brief Query Encoded Header Size
 *
 * @return Size of encoded header
 */
size_t atom::get_header_size() const
{
  switch (data_enc)
  {
    case atom::NONE:
    case atom::TINY:
      return 0;
      break;

    case atom::SHORT:
      return 1;
      break;
      
    case atom::MEDIUM:
      return 2;
      break;
      
    default: // atom::LONG:
      return 4;
      break;
  }
}

/**
 * \brief Get Unsigned Integer Stored as UID (Bytes)
 */
uint64_t atom::get_uid() const
{
  union
  {
    uint64_t flip;
    char     raw[8];
  };
  
  // Unique ID's (UIDs) are quirky. They are 64 bit integers, but get
  // encoded like a byte sequence, of a single length 8 (short).
  // This is simultaneously simpler, and infuriating ...
  if ((data_type != atom::BYTES) || (data_enc != atom::SHORT) || (bytes.size() != 8))
  {
    throw topaz::exception("Invalid UID Atom");
  }
  
  // Extract the bytes
  memcpy(raw, &(bytes[0]), 8);
  
  // Flip to native endianess
  return be64toh(flip);
}

/**
 * \brief Get Unsigned Integer Value
 */
uint64_t atom::get_uint() const
{
  // Sanity check
  if (data_type != atom::UINT)
  {
    throw topaz::exception("Atom is not unsigned integer");
  }
  
  // Pass it back
  return uint_val;
}

/**
 * \brief Get Signed Integer Value
 */
int64_t atom::get_int() const
{
  // Sanity check
  if (data_type != atom::INT)
  {
    throw topaz::exception("Atom is not signed integer");
  }
  
  // Pass it back
  return int_val;
}

/**
 * \brief Get Binary Data
 */
byte_vector const &atom::get_bytes() const
{
  // Sanity check
  if (data_type != atom::BYTES)
  {
    throw topaz::exception("Atom is not binary data");
  }
  
  // Return reference
  return bytes;
}

/**
 * \brief Pick appropriate atom encoding
 *
 * @param byte_count Number of bytes
 */
void atom::pick_encoding(size_t byte_count)
{
  // What's it going to fit in?
  if (byte_count < 16)
  {
    // Small atom (1 byte header)
    data_enc = atom::SHORT;
  }
  else if (byte_count < 2048)
  {
    // Medium atom (2 byte header)
    data_enc = atom::MEDIUM;
  }
  else if (byte_count < 16777216)
  {
    // Long atom (4 byte header)
    data_enc = atom::LONG;
  }
  else
  {
    // Really?
    throw topaz::exception("Atom too large to encode");
  }
}

/**
 * \brief Ensure data to be decoded is of minimum size
 *
 * @param len Length of buffer
 * @param min Minimum size required for buffer
 */
void atom::decode_check_size(size_t len, size_t min) const
{
  if (len < min)
  {
    throw topaz::exception("Atom encoding too short");
  }
}

/**
 * \brief Set atom type based on bin and sign flags
 *
 * @param bits Binary / Sign flags (values 0-3)
 */
void atom::decode_set_type(uint8_t bits)
{
  switch(bits)
  {
    case 0:
      data_type = atom::UINT;
      break;
      
    case 1:
      data_type = atom::INT;
      break;
      
    case 2:
      data_type = atom::BYTES;
      break;
      
    default:
      throw topaz::exception("Invalid / Unhandled atom type");
      break;
  }
}

/**
 * \brief Decode unsigned / signed integer
 */
void atom::decode_int(byte const *data, size_t len)
{
  union
  {
    uint64_t flip;
    byte     raw[8];
  };
  
  // Sanity check
  if ((len == 0) || (len > 8))
  {
    throw topaz::exception("Invalid integer Atom length");
  }
  
  // How many bytes don't get set in raw ...
  int_skip = 8 - len;
  
  // Sign extend negative values
  if ((data_type == atom::INT) && (data[0] & 0x80))
  {
    flip = -1; // Unsigned, so 0xfffffffff ....
  }
  else
  {
    flip = 0;
  }
  
  // Copy data over
  for (size_t i = 0; i < len; i++)
  {
    raw[int_skip + i] = data[i];
  }
  
  // Byteflip (Note: union type)
  uint_val = be64toh(flip);
}
