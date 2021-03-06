/*
 * Boot order test cases.
 *
 * Copyright (c) 2013 Red Hat Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>,
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqos/fw_cfg.h"
#include "libqtest.h"

#include "hw/nvram/fw_cfg_keys.h"

typedef struct {
    const char *args;
    uint64_t expected_boot;
    uint64_t expected_reboot;
} boot_order_test;

static void test_a_boot_order(const char *machine,
                              const char *test_args,
                              uint64_t (*read_boot_order)(void),
                              uint64_t expected_boot,
                              uint64_t expected_reboot)
{
    uint64_t actual;

    global_qtest = qtest_startf("-nodefaults%s%s %s",
                                machine ? " -M " : "",
                                machine ?: "",
                                test_args);
    actual = read_boot_order();
    g_assert_cmphex(actual, ==, expected_boot);
    qmp_discard_response("{ 'execute': 'system_reset' }");
    /*
     * system_reset only requests reset.  We get a RESET event after
     * the actual reset completes.  Need to wait for that.
     */
    qmp_eventwait("RESET");
    actual = read_boot_order();
    g_assert_cmphex(actual, ==, expected_reboot);
    qtest_quit(global_qtest);
}

static void test_boot_orders(const char *machine,
                             uint64_t (*read_boot_order)(void),
                             const boot_order_test *tests)
{
    int i;

    for (i = 0; tests[i].args; i++) {
        test_a_boot_order(machine, tests[i].args,
                          read_boot_order,
                          tests[i].expected_boot,
                          tests[i].expected_reboot);
    }
}

static uint8_t read_mc146818(uint16_t port, uint8_t reg)
{
    outb(port, reg);
    return inb(port + 1);
}

static uint64_t read_boot_order_pc(void)
{
    uint8_t b1 = read_mc146818(0x70, 0x38);
    uint8_t b2 = read_mc146818(0x70, 0x3d);

    return b1 | (b2 << 8);
}

static const boot_order_test test_cases_pc[] = {
    { "",
      0x1230, 0x1230 },
    { "-no-fd-bootchk",
      0x1231, 0x1231 },
    { "-boot c",
      0x0200, 0x0200 },
    { "-boot nda",
      0x3410, 0x3410 },
    { "-boot order=",
      0, 0 },
    { "-boot order= -boot order=c",
      0x0200, 0x0200 },
    { "-boot once=a",
      0x0100, 0x1230 },
    { "-boot once=a -no-fd-bootchk",
      0x0101, 0x1231 },
    { "-boot once=a,order=c",
      0x0100, 0x0200 },
    { "-boot once=d -boot order=nda",
      0x0300, 0x3410 },
    { "-boot once=a -boot once=b -boot once=c",
      0x0200, 0x1230 },
    {}
};

static void test_pc_boot_order(void)
{
    test_boot_orders(NULL, read_boot_order_pc, test_cases_pc);
}

int main(int argc, char *argv[])
{
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        qtest_add_func("boot-order/pc", test_pc_boot_order);
    }
    return g_test_run();
}
