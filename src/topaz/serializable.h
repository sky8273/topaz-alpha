#ifndef TOPAZ_SERIALIZABLE_H
#define TOPAZ_SERIALIZABLE_H

/**
 * File:   $URL $
 * Author: $Author $
 * Date:   $Date $
 * Rev:    $Revision $
 *
 * Topaz - Serializable Object Interface Class
 *
 * This file an object class that can be serialized into a given byte stream,
 * as well as deserialized into a copy of the original object.
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

#include <topaz/defs.h>

namespace topaz
{

  class serializable : public byte_vector
  {
    
  public:
    
    // Constructor / Destructor
    serializable();
    virtual ~serializable();
    
    /**
     * \brief Decode from data buffer
     *
     * @param data Location to read encoded bytes
     * @param len  Length of buffer
     */
    void deserialize(void const *data, size_t len);
    
    /**
     * \brief Decode from data buffer
     *
     * @param data Location to read encoded bytes
     * @param len  Length of buffer
     */
    void deserialize(byte_vector const &data);
    
  protected:
    
    /**
     * \brief Decode internal storage
     */
    virtual void decode() = 0;
    
  };

};

#endif
