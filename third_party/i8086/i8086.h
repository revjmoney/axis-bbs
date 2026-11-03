 

#pragma once

#include <stdint.h>

namespace axis { namespace door { struct MemoryBus; } }

#include <stdint.h>


#define I8086_SHOW_OPCODE_STATS 0
#ifndef I80186MODE
  #define I80186MODE 1
#endif

#ifndef I80186MODE
  #define I80186MODE 0
#endif



namespace fabgl {


class i8086 {

public:

  // callbacks

  typedef void (*WritePort)(void * context, int address, uint8_t value);
  typedef uint8_t (*ReadPort)(void * context, int address);
  typedef bool (*Interrupt)(void * context, int num);


  static void setCallbacks(void * context, ReadPort readPort, WritePort writePort, Interrupt interrupt) {
    s_context            = context;
    s_readPort           = readPort;
    s_writePort          = writePort;
    s_interrupt          = interrupt;
  }

  static void setBus(axis::door::MemoryBus * bus) { s_bus = bus; }

  static void reset();

  static void setAL(uint8_t value);
  static void setAH(uint8_t value);
  static void setBL(uint8_t value);
  static void setBH(uint8_t value);
  static void setCL(uint8_t value);
  static void setCH(uint8_t value);
  static void setDL(uint8_t value);
  static void setDH(uint8_t value);

  static uint8_t AL();
  static uint8_t AH();
  static uint8_t BL();
  static uint8_t BH();
  static uint8_t CL();
  static uint8_t CH();
  static uint8_t DL();
  static uint8_t DH();

  static void setAX(uint16_t value);
  static void setBX(uint16_t value);
  static void setCX(uint16_t value);
  static void setDX(uint16_t value);
  static void setDI(uint16_t value);
  static void setCS(uint16_t value);
  static void setDS(uint16_t value);
  static void setSS(uint16_t value);
  static void setES(uint16_t value);
  static void setIP(uint16_t value);
  static void setSP(uint16_t value);

  static uint16_t AX();
  static uint16_t BX();
  static uint16_t CX();
  static uint16_t DX();
  static uint16_t BP();
  static uint16_t SI();
  static uint16_t DI();
  static uint16_t SP();

  static uint16_t CS();
  static uint16_t ES();
  static uint16_t DS();
  static uint16_t SS();

  static bool flagIF();
  static bool flagTF();
  static bool flagCF();
  static bool flagZF();
  static bool flagOF();
  static bool flagDF();
  static bool flagSF();
  static bool flagAF();
  static bool flagPF();

  static void setFlagZF(bool value);
  static void setFlagCF(bool value);

  static uint16_t IP();

  static bool halted()                                    { return s_halted; }

  static bool IRQ(uint8_t interrupt_num);

  static void step();

  // AXIS: generic register access (index: 0 AX 1 CX 2 DX 3 BX 4 SP 5 BP 6 SI 7 DI 8 ES 9 CS 10 SS 11 DS)
  static uint16_t reg(int index);
  static void     setReg(int index, uint16_t value);
  static uint16_t flagsWord();
  static void     setFlagsWord(uint16_t value);


private:

  static uint8_t WMEM8(int addr, uint8_t value);
  static uint16_t WMEM16(int addr, uint16_t value);
  static uint8_t RMEM8(int addr);
  static uint16_t RMEM16(int addr);

  static uint16_t make_flags();
  static void set_flags(int new_flags);

  static void pc_interrupt(uint8_t interrupt_num);

  static uint8_t raiseDivideByZeroInterrupt();

  static void stepEx(uint8_t const * opcode_stream);


  static void *             s_context;
  static ReadPort           s_readPort;
  static WritePort          s_writePort;
  static Interrupt          s_interrupt;

  static bool               s_pendingIRQ;
  static uint8_t            s_pendingIRQIndex;
  static axis::door::MemoryBus * s_bus;
  static bool               s_halted;

};


} // namespace fabgl
