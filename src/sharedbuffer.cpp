





#include "sharedbuffer.h"
// Constructors ////////////////////////////////////////////////////////////////

sharedbuffer::sharedbuffer() :m_SpinLock (0), buffer(sizeof(u8), 4096, FIFO)
{
}

// Public Methods //////////////////////////////////////////////////////////////

void sharedbuffer::begin()
{
}


// void sharedbuffer::init(const uint32_t dwBaudRate, const uint32_t modeReg)
// {
// }

void sharedbuffer::end( void )
{
}

// int sharedbuffer::availableForWrite(void)
// {
//   int head = _tx_buffer->_iHead;
//   int tail = _tx_buffer->_iTail;
//   if (head >= tail) return SERIAL_BUFFER_SIZE - 1 - head + tail;
//   return tail - head - 1;
// }

int sharedbuffer::peek( void )
{
  m_SpinLock.Acquire();
  if(buffer.isEmpty()){
    m_SpinLock.Release();
    return -1;
  }
  uint8_t uc;
  buffer.peek(&uc);
  m_SpinLock.Release();
  return uc;
}

int sharedbuffer::read( void )
{
  m_SpinLock.Acquire();
  if(buffer.isEmpty()){
    m_SpinLock.Release();
    return -1;
  }
  uint8_t uc;
  buffer.pop(&uc);
  m_SpinLock.Release();
  return uc;
}

void sharedbuffer::flush( void )
{
  m_SpinLock.Acquire();
  buffer.flush();
  m_SpinLock.Release();
}

size_t sharedbuffer::write( const uint8_t uc_data )
{
  m_SpinLock.Acquire();
  buffer.push(&uc_data);
  m_SpinLock.Release();
  return 1;
}

