//-----------------------------------------------------------------------------
// Copyright (C) Proxmark3 contributors. See AUTHORS.md for details.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// See LICENSE.txt for the text of the license.
//-----------------------------------------------------------------------------
// Low frequency PCF7931 commands
//-----------------------------------------------------------------------------
#include "cmdlfpcf7931.h"
#include <string.h>
#include <ctype.h>
#include "cmdparser.h"    // command_t
#include "comms.h"
#include "ui.h"
#include "cliparser.h"

static int CmdHelp(const char *Cmd);

#define PCF7931_DEFAULT_INITDELAY 17500
#define PCF7931_DEFAULT_OFFSET_WIDTH 0
#define PCF7931_DEFAULT_OFFSET_POSITION 0

// Default values - Configuration
static struct pcf7931_config configPcf = {
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    PCF7931_DEFAULT_INITDELAY,
    PCF7931_DEFAULT_OFFSET_WIDTH,
    PCF7931_DEFAULT_OFFSET_POSITION
};

// Resets the configuration settings to default values.
int pcf7931_resetConfig(void) {
    memset(configPcf.Pwd, 0xFF, sizeof(configPcf.Pwd));
    configPcf.InitDelay = PCF7931_DEFAULT_INITDELAY;
    configPcf.OffsetWidth = PCF7931_DEFAULT_OFFSET_WIDTH;
    configPcf.OffsetPosition = PCF7931_DEFAULT_OFFSET_POSITION;
    PrintAndLogEx(INFO, "Configuration reset");
    PrintAndLogEx(HINT, "Hint: try " _YELLOW_("`lf pcf7931 config`") " to view current settings");
    return PM3_SUCCESS;
}

int pcf7931_printConfig(void) {
    PrintAndLogEx(INFO, "Password (LSB first on bytes)... " _YELLOW_("%s"), sprint_hex(configPcf.Pwd, sizeof(configPcf.Pwd)));
    PrintAndLogEx(INFO, "Tag initialization delay........ " _YELLOW_("%d") " us", configPcf.InitDelay);
    PrintAndLogEx(INFO, "Offset low pulses width......... " _YELLOW_("%d") " us", configPcf.OffsetWidth);
    PrintAndLogEx(INFO, "Offset low pulses position...... " _YELLOW_("%d") " us", configPcf.OffsetPosition);
    return PM3_SUCCESS;
}

static int CmdLFPCF7931Reader(const char *Cmd) {
    CLIParserContext *ctx;
    CLIParserInit(&ctx, "lf pcf7931 reader",
                  "read a PCF7931 tag",
                  "lf pcf7931 reader -@   -> continuous reader mode"
                 );

    void *argtable[] = {
        arg_param_begin,
        arg_lit0("@", NULL, "optional - continuous reader mode"),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, true);
    bool cm = arg_get_lit(ctx, 1);
    CLIParserFree(ctx);

    if (cm) {
        PrintAndLogEx(INFO, "Press " _GREEN_("<Enter>") " to exit");
    }

    do {
        PacketResponseNG resp;
        clearCommandBuffer();
        SendCommandNG(CMD_LF_PCF7931_READ, NULL, 0);
        if (WaitForResponseTimeout(CMD_ACK, &resp, 2500) == false) {
            PrintAndLogEx(WARNING, "command execution time out");
            return PM3_ETIMEOUT;
        }
    } while (cm && !kbd_enter_pressed());

    return PM3_SUCCESS;
}

static int CmdLFPCF7931Config(const char *Cmd) {
    CLIParserContext *ctx;
    CLIParserInit(&ctx, "lf pcf7931 config",
                  "This command tries to set the configuration used with PCF7931 commands\n"
                  "The time offsets could be useful to correct slew rate generated by the antenna\n"
                  "Caling without some parameter will print the current configuration.",
                  "lf pcf7931 config --reset\n"
                  "lf pcf7931 config --pwd 11223344556677 -d 20000\n"
                  "lf pcf7931 config --pwd 11223344556677 -d 17500 --lw -10 --lp 30"
                 );

    void *argtable[] = {
        arg_param_begin,
        arg_lit0("r", "reset", "Reset configuration to default values"),
        arg_str0("p", "pwd", "<hex>", "Password, 7bytes, LSB-order"),
        arg_u64_0("d", "delay", "<dec>", "Tag initialization delay (in us)"),
        arg_int0(NULL, "lw", "<dec>", "offset, low pulses width (in us), optional!"),
        arg_int0(NULL, "lp", "<dec>", "offset, low pulses position (in us), optional!"),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, true);

    bool use_reset = arg_get_lit(ctx, 1);
    if (use_reset) {
        CLIParserFree(ctx);
        return pcf7931_resetConfig();
    }

    int pwd_len = 0;
    uint8_t pwd[7] = {0};
    CLIGetHexWithReturn(ctx, 2, pwd, &pwd_len);

    uint32_t delay = arg_get_u32_def(ctx, 3, -1);
    int ow = arg_get_int_def(ctx, 4, 0xFFFF);
    int op = arg_get_int_def(ctx, 5, 0xFFFF);
    CLIParserFree(ctx);

    if (pwd_len && pwd_len < sizeof(pwd)) {
        PrintAndLogEx(ERR, "Password must be 7 bytes");
        return PM3_EINVARG;
    }

    if (pwd_len) {
        memcpy(configPcf.Pwd, pwd, sizeof(configPcf.Pwd));
    }
    if (delay != -1) {
        configPcf.InitDelay = (delay & 0xFFFF);
    }
    if (ow != 0xFFFF) {
        configPcf.OffsetWidth = (ow & 0xFFFF);
    }
    if (op != 0xFFFF) {
        configPcf.OffsetPosition = (op & 0xFFFF);
    }

    pcf7931_printConfig();
    return PM3_SUCCESS;
}

static int CmdLFPCF7931Write(const char *Cmd) {

    CLIParserContext *ctx;
    CLIParserInit(&ctx, "lf pcf7931 write",
                  "This command tries to write a PCF7931 tag.",
                  "lf pcf7931 write --blk 2 --idx 1 -d FF  -> Write 0xFF to block 2, index 1 "
                 );

    void *argtable[] = {
        arg_param_begin,
        arg_u64_1("b", "blk", "<dec>", "[0-7] block number"),
        arg_u64_1("i", "idx", "<dec>", "[0-15] index of byte inside block"),
        arg_str1("d", "data", "<hex>", "one byte to be written"),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, true);
    uint8_t block = arg_get_u32_def(ctx, 1, 0);
    uint8_t idx = arg_get_u32_def(ctx, 2, 0);

    int data_len = 0;
    uint8_t data[1] = {0};
    CLIGetHexWithReturn(ctx, 3, data, &data_len);
    CLIParserFree(ctx);

    if (block > 7) {
        PrintAndLogEx(ERR, "out-of-range error, block must be between 0-7");
        return PM3_EINVARG;
    }

    if (idx > 15) {
        PrintAndLogEx(ERR, "out-of-range error, index must be between 0-15");
        return PM3_EINVARG;
    }

    PrintAndLogEx(INFO, "Writing block %u at idx %u with data 0x%02X", block, idx, data[0]);

    uint32_t buf[10]; // TODO sparse struct, 7 *bytes* then words at offset 4*7!
    memcpy(buf, configPcf.Pwd, sizeof(configPcf.Pwd));
    buf[7] = (configPcf.OffsetWidth + 128);
    buf[8] = (configPcf.OffsetPosition + 128);
    buf[9] = configPcf.InitDelay;

    clearCommandBuffer();
    SendCommandMIX(CMD_LF_PCF7931_WRITE, block, idx, data[0], buf, sizeof(buf));

    PrintAndLogEx(SUCCESS, "Done!");
    PrintAndLogEx(HINT, "Hint: try " _YELLOW_("`lf pcf7931 reader`") " to verify");
    return PM3_SUCCESS;
}

static command_t CommandTable[] = {
    {"help",   CmdHelp,            AlwaysAvailable, "This help"},
    {"reader", CmdLFPCF7931Reader, IfPm3Lf,         "Read content of a PCF7931 transponder"},
    {"write",  CmdLFPCF7931Write,  IfPm3Lf,         "Write data on a PCF7931 transponder."},
    {"config", CmdLFPCF7931Config, AlwaysAvailable, "Configure the password, the tags initialization delay and time offsets (optional)"},
    {NULL, NULL, NULL, NULL}
};

static int CmdHelp(const char *Cmd) {
    (void)Cmd; // Cmd is not used so far
    CmdsHelp(CommandTable);
    return PM3_SUCCESS;
}

int CmdLFPCF7931(const char *Cmd) {
    clearCommandBuffer();
    return CmdsParse(CommandTable, Cmd);
}

