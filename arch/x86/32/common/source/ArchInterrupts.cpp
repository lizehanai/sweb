/**
 * @file ArchInterrupts.cpp
 *
 */

#include "ArchInterrupts.h"
#include "8259.h"
#include "ports.h"
#include "InterruptUtils.h"
#include "SegmentUtils.h"
#include "ArchThreads.h"
#include "assert.h"
#include "Thread.h"

void ArchInterrupts::initialise()
{
  uint16 i; // disableInterrupts();
  initialise8259s();
  SegmentUtils::initialise();
  InterruptUtils::initialise();
  for (i=0;i<16;++i)
    disableIRQ(i);
}

void ArchInterrupts::enableTimer()
{
  enableIRQ(0);
}

void ArchInterrupts::disableTimer()
{
  disableIRQ(0);
}

void ArchInterrupts::enableKBD()
{
  enableIRQ(1);
  enableIRQ(9);
}

void ArchInterrupts::disableKBD()
{
  disableIRQ(1);
}

void ArchInterrupts::EndOfInterrupt(uint16 number) 
{
  sendEOI(number);
}

void ArchInterrupts::enableInterrupts()
{
     __asm__ __volatile__("sti"
   :
   :
   );
}

bool ArchInterrupts::disableInterrupts()
{
   uint32 ret_val;

 __asm__ __volatile__("pushfl\n"
                      "popl %0\n"
                      "cli"
 : "=a"(ret_val)
 :);

return (ret_val & (1 << 9));  //testing IF Flag

}

bool ArchInterrupts::testIFSet()
{
  uint32 ret_val;

  __asm__ __volatile__(
  "pushfl\n"
  "popl %0\n"
  : "=a"(ret_val)
  :);

  return (ret_val & (1 << 9));  //testing IF Flag
}

void ArchInterrupts::yieldIfIFSet()
{
  if (system_state == RUNNING && currentThread && testIFSet())
  {
    ArchThreads::yield();
  }
  else
  {
    __asm__ __volatile__("nop");
  }
}

struct context_switch_registers {
  uint32 es;
  uint32 ds;
  uint32 edi;
  uint32 esi;
  uint32 ebp;
  uint32 esp;
  uint32 ebx;
  uint32 edx;
  uint32 ecx;
  uint32 eax;
};

struct interrupt_registers {
  uint32 eip;
  uint32 cs;
  uint32 eflags;
  uint32 esp3;
  uint32 ss3;
};

extern "C" void arch_saveThreadRegisters(uint32 error)
{
  register struct context_switch_registers* registers;
  registers = (struct context_switch_registers*) (&error + 2);
  register struct interrupt_registers* iregisters;
  iregisters = (struct interrupt_registers*) (&error + 2 + sizeof(struct context_switch_registers)/sizeof(uint32) + (error));
  register ArchThreadRegisters* info = currentThreadRegisters;
  asm("fnsave (%[fpu])\n"
      "frstor (%[fpu])\n"
      :
      : [fpu]"r"(&info->fpu));
  if ((iregisters->cs & 0x3) == 0x3)
  {
    info->ss = iregisters->ss3;
    info->esp = iregisters->esp3;
  }
  else
  {
    info->esp = registers->esp + 0xc;
  }
  info->eip = iregisters->eip;
  info->cs = iregisters->cs;
  info->eflags = iregisters->eflags;
  info->eax = registers->eax;
  info->ecx = registers->ecx;
  info->edx = registers->edx;
  info->ebx = registers->ebx;
  info->ebp = registers->ebp;
  info->esi = registers->esi;
  info->edi = registers->edi;
  info->ds = registers->ds;
  info->es = registers->es;
  assert(!currentThread || currentThread->isStackCanaryOK());
}

extern TSS *g_tss;

extern "C" void arch_contextSwitch()
{
  assert(currentThread->lock_waiting_on_ == 0 && "How did you even manage to execute code while waiting for a lock?");
  assert(currentThread->isStackCanaryOK() && "Kernel stack corruption detected.");
  ArchThreadRegisters info = *currentThreadRegisters; // optimization: local copy produces more efficient code in this case
  if (currentThread->switch_to_userspace_)
  {
    assert(currentThread->holding_lock_list_ == 0 && "Never switch to userspace when holding a lock! Never!");
    asm("push %[ss]" : : [ss]"m"(info.ss));
    asm("push %[esp]" : : [esp]"m"(info.esp));
  }
  else
  {
    asm("mov %[esp], %%esp\n" : : [esp]"m"(info.esp));
  }
  g_tss->esp0 = info.esp0;
  asm("frstor (%[fpu])\n" : : [fpu]"r"(&info.fpu));
  asm("mov %[cr3], %%cr3\n" : : [cr3]"r"(info.cr3));
  asm("push %[eflags]\n" : : [eflags]"m"(info.eflags));
  asm("push %[cs]\n" : : [cs]"m"(info.cs));
  asm("push %[eip]\n" : : [eip]"m"(info.eip));
  asm("mov %[esi], %%esi\n" : : [esi]"m"(info.esi));
  asm("mov %[edi], %%edi\n" : : [edi]"m"(info.edi));
  asm("mov %[es], %%es\n" : : [es]"m"(info.es));
  asm("mov %[ds], %%ds\n" : : [ds]"m"(info.ds));
  asm("push %[ebp]\n" : : [ebp]"m"(info.ebp));
  asm("pop %%ebp\n"
      "iret" : : "a"(info.eax), "b"(info.ebx), "c"(info.ecx), "d"(info.edx));
  asm("hlt");
  assert(false);
}
