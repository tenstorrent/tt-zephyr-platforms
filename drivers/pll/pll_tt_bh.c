#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bh_tt_pll);

#define DT_DRV_COMPAT tenstorrent_bh_pll

typedef struct {
	uint32_t reset: 1;
	uint32_t pd: 1;
	uint32_t reset_lock: 1;
	uint32_t pd_bgr: 1;
	uint32_t bypass: 1;
} PLL_CNTL_PLL_CNTL_0_reg_t;

typedef union {
	uint32_t val;
	PLL_CNTL_PLL_CNTL_0_reg_t f;
} PLL_CNTL_PLL_CNTL_0_reg_u;

typedef struct {
	uint32_t refdiv: 8;
	uint32_t postdiv: 8;
	uint32_t fbdiv: 16;
} PLL_CNTL_PLL_CNTL_1_reg_t;

typedef union {
	uint32_t val;
	PLL_CNTL_PLL_CNTL_1_reg_t f;
} PLL_CNTL_PLL_CNTL_1_reg_u;

typedef struct {
	uint32_t ctrl_bus1: 8;
	uint32_t ctrl_bus2: 8;
	uint32_t ctrl_bus3: 8;
	uint32_t ctrl_bus4: 8;
} PLL_CNTL_PLL_CNTL_2_reg_t;

typedef union {
	uint32_t val;
	PLL_CNTL_PLL_CNTL_2_reg_t f;
} PLL_CNTL_PLL_CNTL_2_reg_u;

typedef struct {
	uint32_t ctrl_bus5: 8;
	uint32_t test_bus: 8;
	uint32_t lock_detect1: 16;
} PLL_CNTL_PLL_CNTL_3_reg_t;

typedef union {
	uint32_t val;
	PLL_CNTL_PLL_CNTL_3_reg_t f;
} PLL_CNTL_PLL_CNTL_3_reg_u;

typedef struct {
	uint32_t lock_detect2: 16;
	uint32_t lock_detect3: 16;
} PLL_CNTL_PLL_CNTL_4_reg_t;

typedef union {
	uint32_t val;
	PLL_CNTL_PLL_CNTL_4_reg_t f;
} PLL_CNTL_PLL_CNTL_4_reg_u;

typedef struct {
	uint32_t postdiv0: 8;
	uint32_t postdiv1: 8;
	uint32_t postdiv2: 8;
	uint32_t postdiv3: 8;
} PLL_CNTL_PLL_CNTL_5_reg_t;

typedef union {
	uint32_t val;
	PLL_CNTL_PLL_CNTL_5_reg_t f;
} PLL_CNTL_PLL_CNTL_5_reg_u;

typedef struct {
	uint32_t pll_use_postdiv0: 1;
	uint32_t pll_use_postdiv1: 1;
	uint32_t pll_use_postdiv2: 1;
	uint32_t pll_use_postdiv3: 1;
	uint32_t pll_use_postdiv4: 1;
	uint32_t pll_use_postdiv5: 1;
	uint32_t pll_use_postdiv6: 1;
	uint32_t pll_use_postdiv7: 1;
} PLL_CNTL_USE_POSTDIV_reg_t;

typedef union {
	uint32_t val;
	PLL_CNTL_USE_POSTDIV_reg_t f;
} PLL_CNTL_USE_POSTDIV_reg_u;

typedef struct {
	uint32_t pll0_lock: 1;
	uint32_t pll1_lock: 1;
	uint32_t pll2_lock: 1;
	uint32_t pll3_lock: 1;
	uint32_t pll4_lock: 1;
} PLL_CNTL_WRAPPER_PLL_LOCK_reg_t;

typedef union {
	uint32_t val;
	PLL_CNTL_WRAPPER_PLL_LOCK_reg_t f;
} PLL_CNTL_WRAPPER_PLL_LOCK_reg_u;

struct pll_tt_bh_config {
    int pll_stride;

	int pll_index;
	PLL_CNTL_PLL_CNTL_1_reg_u pll_cntl_1;
	PLL_CNTL_PLL_CNTL_2_reg_u pll_cntl_2;
	PLL_CNTL_PLL_CNTL_3_reg_u pll_cntl_3;
	PLL_CNTL_PLL_CNTL_5_reg_u pll_cntl_5;
	PLL_CNTL_USE_POSTDIV_reg_u use_postdiv;
};

struct pll_tt_bh_data {

};

static int pll_tt_bh_on(const struct device *dev, clock_control_subsys_t sys)
{
    return -ENOSYS;
}

static int pll_tt_bh_off(const struct device *dev, clock_control_subsys_t sys)
{
    return -ENOSYS;
}

static int pll_tt_bh_async_on(const struct device *dev, clock_control_subsys_t sys,
                           clock_control_cb_t cb, void *user_data)
{
    return -ENOSYS;
}

static int pll_tt_bh_get_rate(const struct device *dev, clock_control_subsys_t sys, uint32_t *rate)
{
    return -ENOSYS;
}

static enum clock_control_status pll_tt_bh_get_status(const struct device *dev,
                                                   clock_control_subsys_t sys)
{
    return CLOCK_CONTROL_STATUS_UNKNOWN;
}

static int pll_tt_bh_set_rate(const struct device *dev, clock_control_subsys_t sys,
                           clock_control_subsys_rate_t rate)
{
    return -ENOSYS;
}

static int pll_tt_bh_configure(const struct device *dev, clock_control_subsys_t sys,
                            void *data)
{
    return -ENOSYS;
}

static int pll_tt_bh_init(const struct device *dev)
{
    return -ENOSYS;
}

static const struct clock_control_driver_api pll_tt_bh_api = {
    .on = pll_tt_bh_on,
    .off = pll_tt_bh_off,
    .async_on = pll_tt_bh_async_on,
    .get_rate = pll_tt_bh_get_rate,
    .get_status = pll_tt_bh_get_status,
    .set_rate = pll_tt_bh_set_rate,
    .configure = pll_tt_bh_configure
};

#define PLL_TT_BH_INIT(_inst)                                                                            \
    static struct pll_tt_bh_data pll_tt_bh_data_##_inst;                                                    \
    static const struct pll_tt_bh_config pll_tt_bh_config_##_inst = {                                       \
        .pll_index = _inst,                                      \
		.pll_cntl_1.f = {.refdiv = DT_INST_PROP(_inst, refdiv), \
                         .fbdiv = DT_INST_PROP(_inst, fbdiv)}, \
		.pll_cntl_2.f = {.ctrl_bus1 = DT_INST_PROP(_inst, ctrl_bus1)},                          \
		.pll_cntl_3.f = {.ctrl_bus5 = DT_INST_PROP(_inst, ctrl_bus5)},                          \
		.pll_cntl_5.f = {.postdiv0 = DT_INST_PROP(_inst, postdiv0),                             \
				         .postdiv1 = DT_INST_PROP(_inst, postdiv1),                             \
				         .postdiv2 = DT_INST_PROP(_inst, postdiv2),                             \
				         .postdiv3 = DT_INST_PROP(_inst, postdiv3)},                            \
		.use_postdiv.val = DT_INST_PROP(_inst, use_postdiv), \
    };                                                                                            \
                                                                                                  \
    DEVICE_DT_INST_DEFINE(_inst, &pll_tt_bh_init, NULL, &pll_tt_bh_data_##_inst, &pll_tt_bh_config_##_inst,             \
                          POST_KERNEL, 3, &pll_tt_bh_api);

DT_INST_FOREACH_STATUS_OKAY(PLL_TT_BH_INIT)
