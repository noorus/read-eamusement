#define NOMINMAX
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include <exception>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdint>

#include "hfrdapi.h"

using std::string;
using std::vector;
using std::exception;

class myexception: public std::exception {
private:
  string description_;
  int code_;
public:
  myexception( const string& from, int code ): code_( code )
  {
    char tmp[128];
    sprintf_s( tmp, 128, "%s threw up: error code %02X", from.c_str(), code );
    description_ = tmp;
  }

  const char* what() const noexcept override
  {
    return description_.c_str();
  }
};

#include "cipher.cpp"

struct Card {
  uint8_t dsf_ = 0;
  uint8_t uid_[8] = { 0 };
  enum Type {
    CardType_Unknown,
    CardType_eAmuse,
    CardType_FeliCa
  } type_ = CardType_Unknown;
  string kcode_;
};

class Reader {
private:
  HID_DEVICE handle_ = HID_DEVICE( -1 );
  uint16_t vid_ = 0x0416;
  uint16_t pid_ = 0x8020;
  string serial_;
  string model_;
  Card card_;
public:
  enum LightColor: uint8_t {
    Light_Off = 0,
    Light_Red,
    Light_Yellow,
    Light_Green
  };
  uint32_t version() const
  {
    unsigned long out;
    auto ret = Sys_GetLibVersion( &out );
    if ( ret != 0 )
      throw myexception( "Sys_GetDeviceNum", ret );
    return out;
  }
  uint32_t count() const
  {
    unsigned long out;
    auto ret = Sys_GetDeviceNum( vid_, pid_, &out );
    if ( ret != 0 )
      throw myexception( "Sys_GetDeviceNum", ret );
    return out;
  }
  void open( uint32_t index )
  {
    if ( Sys_IsOpen( handle_ ) )
      Sys_Close( &handle_ );
    auto ret = Sys_Open( &handle_, static_cast<DWORD>( index ), vid_, pid_ );
    if ( ret != 0 )
      throw myexception( "Sys_Open", ret );

    char temp[256] = { 0 };
    ret = Sys_GetHidSerialNumberStr( static_cast<DWORD>( index ), vid_, pid_, temp, 256 );
    if ( ret != 0 )
      throw myexception( "Sys_GetHidSerialNumberStr", ret );
    serial_ = temp;

    memset( temp, 0, 256 );
    uint8_t len = 256;
    ret = Sys_GetModel( handle_, reinterpret_cast<BYTE*>( temp ), &len );
    if ( ret != 0 )
      throw myexception( "Sys_GetModel", ret );
    model_ = temp;
  }
  inline const string& serial() const noexcept { return serial_; }
  inline const string& model() const noexcept { return model_; }
  inline const Card& card() const noexcept { return card_; }
  void light( LightColor color )
  {
    auto ret = Sys_SetLight( handle_, static_cast<BYTE>( color ) );
    if ( ret != 0 )
      throw myexception( "Sys_SetLight", ret );
  }
  void antenna( bool enable )
  {
    auto ret = Sys_SetAntenna( handle_, enable ? 1 : 0 );
    if ( ret != 0 )
      throw myexception( "Sys_SetAntenna", ret );
  }
  bool readCard()
  {
    auto ret = Sys_InitType( handle_, '1' );
    if ( ret != 0 )
      throw myexception( "Sys_InitType", ret );

    vector<uint8_t> buffer( 9, 0 );
    uint8_t len = buffer.size();
    ret = I15693_Inventory( handle_, buffer.data(), &len );
    if ( ret == 0x14 )
      return false;
    if ( ret != 0 )
      throw myexception( "I15693_Inventory", ret );
    if ( len >= 9 )
    {
      Sys_SetBuzzer( handle_, 10 );
      if ( buffer.size() > 9 )
        buffer.resize( 9 );
      std::reverse( buffer.begin(), buffer.end() );
      memcpy( card_.uid_, buffer.data(), 8 );
      card_.dsf_ = buffer[8];
      if ( card_.uid_[0] == 0xE0 )
        card_.type_ = Card::CardType_eAmuse;
      else if ( card_.uid_[0] == 0x01 )
        card_.type_ = Card::CardType_FeliCa;
      buffer.resize( 8 );
      std::reverse( buffer.begin(), buffer.end() );
      card_.kcode_ = cardcipher::encodePretty( buffer.data() );
      return true;
    }
    return false;
  }
  void close()
  {
    Sys_Close( &handle_ );
  }
};

int wmain( int argc, wchar_t** argv, wchar_t** env )
{
  try
  {
    Reader reader;

    auto count = reader.count();
    if ( count < 1 )
      throw std::exception( "no devices found" );

    for ( unsigned long i = 0; i < count; ++i )
    {
      reader.open( i );

      printf( "device %i serial %s model %s\n", i, reader.serial().c_str(), reader.model().c_str() );

      reader.light( Reader::Light_Yellow );
      reader.antenna( false );
      Sleep( 500 );
      reader.light( Reader::Light_Red );
      reader.antenna( true );
      if ( reader.readCard() )
      {
        const auto& card = reader.card();
        printf( "read card:\n" );
        printf( "- type %i (%s), dsf 0x%02x\n",
          card.type_,
          card.type_ == Card::CardType_Unknown ? "unknown"
          : card.type_ == Card::CardType_eAmuse ? "eAmusement"
          : "FeliCa",
          card.dsf_ );
        printf( "- uid 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
          card.uid_[0], card.uid_[1], card.uid_[2], card.uid_[3],
          card.uid_[4], card.uid_[5], card.uid_[6], card.uid_[7] );
        printf( "- eAmuse code: %s\n", card.kcode_.c_str() );
      }
      else
      {
        printf( "no card found\n" );
      }

      reader.light( Reader::Light_Off );
      reader.close();
    }

    return EXIT_SUCCESS;
  }
  catch ( std::exception& e )
  {
    printf( "exception: %s\n", e.what() );
    return EXIT_FAILURE;
  }
}