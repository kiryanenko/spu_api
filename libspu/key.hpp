/*
  key.hpp
        - key class definition and implementation (because it is a template)
        - class compile u32 number with SPU_WEIGHT from given fields

  Copyright 2019  Dubrovin Egor <dubrovin.en@ya.ru>
                  Alex Popov <alexpopov@bmsru.ru>
                  Bauman Moscow State Technical University

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef KEY_HPP
#define KEY_HPP

#include "spu.h"

#include <vector>

namespace SPU
{

/* Class to provide key as bit flow */
template<typename FieldNameType> // Type to define a field name in associate arrays
class Key
{
public:
  /* Container for fields length definitions */
  struct FieldLengthStruct {
    FieldNameType name;
    u32 length;
  };

  /* Container for fields data definitions */
  struct FieldDataStruct {
    FieldNameType name;
    u32 data; // Max data for one field is 32 bits
  };

  /* Vector class hiders */
  typedef std::vector<FieldLengthStruct> FiledsLengthVector;
  typedef std::vector<FieldDataStruct> FieldsDataVector;

private:
  FiledsLengthVector fields_len_vec; // Fields length definition vector

  /* Construct key field mask */
  u32 mask(u32 len);

  /* Iterates in FieldsDataVector to find first data with equals name */
  u32 find_data_by_name(FieldsDataVector fields_data_vector, FieldNameType name);

public:
  /* Constructor from length vector */
  Key(FiledsLengthVector fields_length_vector) : fields_len_vec(fields_length_vector) {}

  /* Key compilation */
  bool compileKey(u32 key[SPU_WEIGHT], FieldsDataVector fields_data_vector);
};

/* Key field mask construct */
/* Actually a decoder from length to length number of ones */
template<typename FieldNameType>
u32 Key<FieldNameType>::mask(u32 len)
{
  u32 mask = 0x0;
  for (u32 i = 0; i < len; i++)
  {
    mask |= 1 << i; // Shifting 1 to i and concatenate with current mask
  }
  return mask;
}

/* Iterates in FieldsDataVector to find first data with equals name */
template<typename FieldNameType>
u32 Key<FieldNameType>::find_data_by_name(FieldsDataVector fields_data_vector, FieldNameType name)
{
  for (auto field_data_struct : fields_data_vector)
  {
    if (field_data_struct.name == name)
    {
      return field_data_struct.data;
    }
  }
  return 0x0;
}

/* Key construction */
template<typename FieldNameType>
bool Key<FieldNameType>::compileKey(u32 key[SPU_WEIGHT], FieldsDataVector fields_data_vector)
{
  u32 shift = 0; // Shift at current iteration (length at previous)
  u8 weight = 0; // Current 32 bit unit in key width

  /* Iterates in FiledsLengthVector and masking and concatenating key with shift */
  for (auto field_length_struct : fields_len_vec)
  {
    /* Field at current step */
    u32 field_data = find_data_by_name(fields_data_vector, field_length_struct.name);
    u32 field_mask = mask(field_length_struct.length);
    u32 field = field_data & field_mask; // Masking

    /* Add field to key */
    key[weight] |= field << shift; // Shifting
    
    /* Iterate in key */
    u32 prev_shift = shift;
    shift += field_length_struct.length;

    /* Check if outside current weight */
    if(shift >= 32 && weight < SPU_WEIGHT-1)
    {
      /* Convert shift into weight */
      shift -= 32;
      weight++;

      /* Shift field for to weight 0 position */
      field >>= field_length_struct.length - prev_shift;

      /* Add residual */
      u32 portion = mask(shift);
      key[weight] |= field & portion;
    }
  }

  return true;
}

} /* namespace SPU */

#endif /* KEY_HPP */