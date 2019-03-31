/*
  cmdexec.c
        - SPU commands executors

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

/* Define local logging object - current part of driver */
#undef LOG_OBJECT
#define LOG_OBJECT "command execution"

#include <linux/slab.h>

#include "spu.h"
#include "log.h"
#include "pcidrv.h"
#include "cmdexec.h"
#include "gsidresolver.h"

/* Internal functions */
static size_t alloc_rslt(const void **res_buf, u8 cmd);
static void adds(const void *res_buf);
static int init_burst_w(struct pci_burst *pci_burst, u8 cmd, const void *cmd_buf);
static int init_burst_r(struct pci_burst *pci_burst, u8 cmd);
static void set_rsltfrmt(struct pci_burst *pci_burst, u8 cmd, const void *res_buf);

/* Commands execution in command workflow */
size_t execute_cmd(const void *cmd_buf, const void **res_buf)
{
  u8 poll_delay = 0;
  size_t rslt_size = 0;
  struct pci_burst pci_burst_w, pci_burst_r;

  /* Set up command number from format 0 */
  u8 cmd = CMDFRMT_0(cmd_buf)->cmd;
  u8 pure_cmd = cmd&CMD_MASK;
  LOG_DEBUG("Executing command 0x%02x with flag Q %d and flag R %d", pure_cmd, GET_Q_FLAG(cmd), GET_R_FLAG(cmd));

  /* Allocate result structure */
  rslt_size = alloc_rslt(res_buf, pure_cmd);

  /* Special case ADDS command - no PCI transactions need */
  if(pure_cmd == ADDS)
  {
    adds(*res_buf);
    return rslt_size;
  }

  /* Init to-write burst structure */
  if(init_burst_w(&pci_burst_w, pure_cmd, cmd_buf) != 0)
  {
    LOG_ERROR("Could not initialize to-write burst structure");
    return -ENOMEM;
  }  

  /* Init burst to-read structure */
  if(init_burst_r(&pci_burst_r, pure_cmd) != 0)
  {
    LOG_ERROR("Could not initialize burst to-read structure");
    return -ENOMEM;
  }
  LOG_DEBUG("PCI burst structures initialized");

  /* Execute command and poll SPU ready state */
  pci_burst_write(&pci_burst_w);
  do
  {
    LOG_DEBUG("Polling command execution end");
    for (poll_delay=0; poll_delay<0xff; poll_delay++);
  }
  while ( (pci_single_read(STATE_REG_0) & (1<<SPU_READY_FLAG)) == 0 );
  LOG_DEBUG("SPU complete command execution");

  /* Read results */
  pci_burst_read(&pci_burst_r);
  set_rsltfrmt(&pci_burst_r, pure_cmd, *res_buf);
  LOG_DEBUG("Got results of operation");

  /* Kill burst structures */
  if(pci_burst_w.addr_shift)
  {
    kfree(pci_burst_w.addr_shift);
  }
  if(pci_burst_w.data)
  {
    kfree(pci_burst_w.data);
  }
  if(pci_burst_r.addr_shift)
  {
    kfree(pci_burst_r.addr_shift);
  }
  if(pci_burst_r.data)
  {
    kfree(pci_burst_r.data);
  }
  LOG_DEBUG("PCI burst structures deleted");

  /* Return */
  return rslt_size;
}

/* Allocate result structure */
static size_t alloc_rslt(const void **res_buf, u8 cmd)
{
  size_t rslt_size = 0;

  switch(cmd)
  {
    CASE_RSLTFRMT_0:
      LOG_DEBUG("Allocate result format 0 structure");
      rslt_size = sizeof(struct rsltfrmt_0);
      break;

    CASE_RSLTFRMT_1:
      LOG_DEBUG("Allocate result format 1 structure");
      rslt_size = sizeof(struct rsltfrmt_1);
      break;

    CASE_RSLTFRMT_2:
      LOG_DEBUG("Allocate result format 2 structure");
      rslt_size = sizeof(struct rsltfrmt_2);
      break;

    default:
      LOG_ERROR("Command was not found to allocate result");
      return -ENOEXEC;
  }

  /* Allocate */
  *res_buf = kmalloc(rslt_size, GFP_KERNEL);
  if(!(*res_buf))
  {
    LOG_ERROR("Could not allocate result structure");
    return -ENOMEM;
  }
  LOG_DEBUG("Allocate result with size %d", rslt_size);

  /* Set standard error return code */
  RSLTFRMT_0(*res_buf)->rslt = ERR;

  return rslt_size;
}

/* ADDS command executor */
static void adds(const void *res_buf)
{
  LOG_DEBUG("ADDS command execution");

  /* Result generation */
  if(create_gsid(RSLTFRMT_0(res_buf)->gsid) != 0)
  {
    LOG_ERROR("ADDS command execution error");
    return; // ERR result code already in result structure
  }
  RSLTFRMT_0(res_buf)->rslt = OK;

  LOG_DEBUG("ADDS return result");
}

/* Initialize burst to-write structure */
static int init_burst_w(struct pci_burst *pci_burst, u8 cmd, const void *cmd_buf)
{
  u8 count = 0;
  int str, str_a, str_b, str_r;
  u8 i;

  switch(cmd)
  {
    CASE_CMDFRMT_1:
      LOG_DEBUG("Allocate to-write burst structure for command format 1");

      /* Get structure number in SPU and create execution possibility */
      str = resolve_gsid(CMDFRMT_1(cmd_buf)->gsid, cmd);
      if(str <= 0)
      {
        LOG_ERROR("GSID" GSID_FORMAT "was not found", GSID_VAR(CMDFRMT_1(cmd_buf)->gsid));
        return -ENOKEY;
      }

      /* Burst count formula: key + val + cmd/str */
      count = SPU_WEIGHT*2 + 1;
      break;

    CASE_CMDFRMT_2:
      LOG_DEBUG("Allocate to-write burst structure for command format 2");

      /* Get structure number in SPU and create execution possibility */
      str = resolve_gsid(CMDFRMT_2(cmd_buf)->gsid, cmd);
      if(str <= 0)
      {
        LOG_ERROR("GSID" GSID_FORMAT "was not found", GSID_VAR(CMDFRMT_2(cmd_buf)->gsid));
        return -ENOKEY;
      }

      /* Burst count formula: key + cmd/str */
      count = SPU_WEIGHT + 1;
      break;

    CASE_CMDFRMT_3:
      LOG_DEBUG("Allocate to-write burst structure for command format 3");

      /* Get structure number in SPU and create execution possibility */
      str = resolve_gsid(CMDFRMT_3(cmd_buf)->gsid, cmd);
      if(str <= 0)
      {
        LOG_ERROR("GSID" GSID_FORMAT "was not found", GSID_VAR(CMDFRMT_3(cmd_buf)->gsid));
        return -ENOKEY;
      }

      /* Burst count formula: cmd/str */
      count = 1;
      break;

    CASE_CMDFRMT_4:
      LOG_DEBUG("Allocate to-write burst structure for command format 4");

      /* Get structure number in SPU and create execution possibility */
      str_a = resolve_gsid(CMDFRMT_4(cmd_buf)->gsid_a, cmd);
      if(str_a <= 0)
      {
        LOG_ERROR("A GSID" GSID_FORMAT "was not found", GSID_VAR(CMDFRMT_4(cmd_buf)->gsid_a));
        return -ENOKEY;
      }
      str_b = resolve_gsid(CMDFRMT_4(cmd_buf)->gsid_b, cmd);
      if(str_b <= 0)
      {
        LOG_ERROR("B GSID" GSID_FORMAT "was not found", GSID_VAR(CMDFRMT_4(cmd_buf)->gsid_b));
        return -ENOKEY;
      }
      str_r = resolve_gsid(CMDFRMT_4(cmd_buf)->gsid_r, cmd);
      if(str_b <= 0)
      {
        LOG_ERROR("R GSID" GSID_FORMAT "was not found", GSID_VAR(CMDFRMT_4(cmd_buf)->gsid_r));
        return -ENOKEY;
      }

      /* Burst count formula: cmd/str */
      count = 1;
      break;

    CASE_CMDFRMT_5:
      LOG_DEBUG("Allocate to-write burst structure for command format 5");

      /* Get structure number in SPU and create execution possibility */
      str_a = resolve_gsid(CMDFRMT_5(cmd_buf)->gsid_a, cmd);
      if(str_a <= 0)
      {
        LOG_ERROR("GSID" GSID_FORMAT "was not found", GSID_VAR(CMDFRMT_5(cmd_buf)->gsid_a));
        return -ENOKEY;
      }
      str_r = resolve_gsid(CMDFRMT_5(cmd_buf)->gsid_r, cmd);
      if(str_b <= 0)
      {
        LOG_ERROR("GSID" GSID_FORMAT "was not found", GSID_VAR(CMDFRMT_5(cmd_buf)->gsid_r));
        return -ENOKEY;
      }

      /* Burst count formula: cmd/str */
      count = 1;
      break;

    default:
      LOG_ERROR("Command was not found to allocate burst to-write structure");
      return -ENOEXEC;
  }

  /* Allocate to-write burst structure */
  pci_burst->count      = count;
  pci_burst->addr_shift = kmalloc(count*sizeof(u32), GFP_KERNEL);
  pci_burst->data       = kmalloc(count*sizeof(u32), GFP_KERNEL);

  if(!pci_burst->addr_shift || !pci_burst->data)
  {
    LOG_ERROR("Could not allocate to-write burst structure");
    return -ENOMEM;
  }
  LOG_DEBUG("Burst to-write structure allocated");

  /* Initialize burst structure */
  switch(cmd)
  {
    CASE_CMDFRMT_1:
      LOG_DEBUG("Initialize to-write burst structure for command format 1");

      /* Initialize in loop */
      for(i=0; i<SPU_WEIGHT; i++)
      {
        /* Set key */
        pci_burst->addr_shift[i] = KEY_REG + i;
        pci_burst->data[i]       = CMDFRMT_1(cmd_buf)->key[i];

        /* Set value */
        pci_burst->addr_shift[i+SPU_WEIGHT] = VAL_REG + i;
        pci_burst->data[i+SPU_WEIGHT]       = CMDFRMT_1(cmd_buf)->val[i];
      }

      /* Last one is a command */
      pci_burst->addr_shift[count-1] = CMD_REG;
      pci_burst->data[count-1]       = CMD_SHIFT(CMDFRMT_1(cmd_buf)->cmd) | str;

      break;

    CASE_CMDFRMT_2:
      LOG_DEBUG("Initialize to-write burst structure for command format 2");

      /* Initialize in loop */
      for(i=0; i<SPU_WEIGHT; i++)
      {
        /* Set key */
        pci_burst->addr_shift[i] = KEY_REG + i;
        pci_burst->data[i]       = CMDFRMT_2(cmd_buf)->key[i];
      }

      /* Last one is a command */
      pci_burst->addr_shift[count-1] = CMD_REG;
      pci_burst->data[count-1]       = CMD_SHIFT(CMDFRMT_2(cmd_buf)->cmd) | str;

      break;

    CASE_CMDFRMT_3:
      LOG_DEBUG("Initialize to-write burst structure for command format 3");

      /* Last just a command */
      pci_burst->addr_shift[0] = CMD_REG;
      pci_burst->data[0]       = CMD_SHIFT(CMDFRMT_3(cmd_buf)->cmd) | str;

      break;

    CASE_CMDFRMT_4:
      LOG_DEBUG("Initialize to-write burst structure for command format 4");

      /* Last just a command */
      pci_burst->addr_shift[0] = CMD_REG;
      pci_burst->data[0]       = CMD_SHIFT(CMDFRMT_4(cmd_buf)->cmd) |
                                 STR_A_SHIFT(str_a) |
                                 STR_B_SHIFT(str_b) |
                                 STR_R_SHIFT(str_r);

      break;

    CASE_CMDFRMT_5:
      LOG_DEBUG("Initialize to-write burst structure for command format 5");

      /* Last just a command */
      pci_burst->addr_shift[0] = CMD_REG;
      pci_burst->data[0]       = CMD_SHIFT(CMDFRMT_5(cmd_buf)->cmd) |
                                 STR_A_SHIFT(str_a) |
                                 STR_R_SHIFT(str_r);

      break;

    default:
      LOG_ERROR("Could not initialize burst to-write structure");
      return -ENOMEM;
  }

  return 0;
}

/* Initialize burst to-read structure */
static int init_burst_r(struct pci_burst *pci_burst, u8 cmd)
{
  u8 count = 0;
  u8 i;

  switch(cmd)
  {
    CASE_RSLTFRMT_1:
      LOG_DEBUG("Allocate to-read burst structure for result format 1");

      /* Burst count formula: power */
      count = 1;
      break;

    CASE_RSLTFRMT_2:
      LOG_DEBUG("Allocate to-read burst structure for result format 2");

      /* Burst count formula: key + val + power */
      count = SPU_WEIGHT*2 + 1;
      break;

    default:
      LOG_ERROR("Could not allocate burst to-read structure");
      return -ENOEXEC;
  }

  /* Allocate burst to-read structure */
  pci_burst->count      = count;
  pci_burst->addr_shift = kmalloc(count*sizeof(u32), GFP_KERNEL);
  pci_burst->data       = kmalloc(count*sizeof(u32), GFP_KERNEL);

  if(!pci_burst->addr_shift || !pci_burst->data)
  {
    LOG_ERROR("Could not allocate burst to-read structure");
    return -ENOMEM;
  }
  LOG_DEBUG("Allocate burst to-read structure ");

  /* Initialize burst structure */
  switch(cmd)
  {
    CASE_RSLTFRMT_1:
      LOG_DEBUG("Init burst to-read structure for result format 1");

      /* Just a power */
      pci_burst->addr_shift[0] = POWER_REG;
      break;


    CASE_RSLTFRMT_2:
      LOG_DEBUG("Init burst to-read structure for result format 2");

      /* Initialize in loop key and value addresses */
      for(i=0; i<SPU_WEIGHT; i++)
      {
        pci_burst->addr_shift[i]            = KEY_REG + i;
        pci_burst->addr_shift[i+SPU_WEIGHT] = VAL_REG + i;
      }

      /* Last one is a power */
      pci_burst->addr_shift[count-1] = POWER_REG;
      break;

    default:
      LOG_ERROR("Could not initialize burst to-read structure");
      return -ENOMEM;
  }

  return 0;
}

/* Set result output format */
static void set_rsltfrmt(struct pci_burst *pci_burst, u8 cmd, const void *res_buf)
{
  u8 count = 0;
  u8 i;

  switch(cmd)
  {
    CASE_RSLTFRMT_1:
      LOG_DEBUG("Set result for format 1");
      RSLTFRMT_1(res_buf)->rslt = OK;

      /* Just a power */
      RSLTFRMT_1(res_buf)->power = pci_burst->data[count-1];
      break;

    CASE_RSLTFRMT_2:
      LOG_DEBUG("Set result for format 2");
      RSLTFRMT_2(res_buf)->rslt = OK;

      /* Initialize in loop key and value */
      for(i=0; i<SPU_WEIGHT; i++)
      {
        RSLTFRMT_2(res_buf)->key[i] = pci_burst->data[i];
        RSLTFRMT_2(res_buf)->val[i] = pci_burst->data[i+SPU_WEIGHT];
      }

      /* Last one is a power */
      RSLTFRMT_2(res_buf)->power = pci_burst->data[count-1];
      break;

    default:
      LOG_ERROR("Could not set result");
      break;
  }
}