/*
    AT24xxx

 * [x] AT24C02 指令 xxx(地址)xxxx(地址高)x(r/w) + 1字节地址低
 * [x] eeprom device / i2c device(使用i2c设备驱动框架层的接口)
 * [x] read
 * [x] write(支持跨页写)  跨页写回回环覆盖,而不是写到新的一页
 */

#include <rthw.h>
#include <rtthread.h>
#include <rtdevice.h>

#define DBG_TAG "EEPROM"
#define DBG_LVL               DBG_LOG
#include <rtdbg.h>

struct AT24xxx_eeprom_config_t
{
    // todo:增加AT24多种型号芯片兼容性
    char *bus;
    rt_uint16_t size;
    rt_uint16_t page_size;
    rt_uint8_t device_addr;

};

struct AT24xxx_eeprom_device_t
{
    struct rt_device parent;
    struct AT24xxx_eeprom_config_t *config;
    struct rt_i2c_bus_device *bus;
};

static rt_ssize_t AT24xxx_eeprom_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    rt_uint8_t r_addr, r_size;
    struct rt_i2c_msg msgs[2] = {0};
    struct AT24xxx_eeprom_device_t *eeprom = RT_NULL;
    struct AT24xxx_eeprom_config_t *config = RT_NULL;
    struct rt_i2c_bus_device *bus = RT_NULL;

    RT_ASSERT(dev);

    eeprom = (struct AT24xxx_eeprom_device_t *)dev;
    config = eeprom->config;
    bus = eeprom->bus;

    RT_ASSERT((pos + size) <= config->size);

    r_addr = pos % 256;
    r_size = size > 256 ? 256 : size;

    msgs[0].addr = (config->device_addr >> 1) + (pos / 256);
    msgs[0].flags |= RT_I2C_WR;
    msgs[0].len = 1;
    msgs[0].buf = &r_addr;

    msgs[1].addr = (config->device_addr >> 1) + (pos / 256);
    msgs[1].flags |= RT_I2C_RD;
    msgs[1].len = r_size;
    msgs[1].buf = (rt_uint8_t *)buffer;

    return rt_i2c_transfer(bus, msgs, 2);
}

static rt_ssize_t AT24xxx_eeprom_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    rt_ssize_t rlt = 0;
    rt_uint8_t r_addr = 0, r_size = 0, temp_size = 0, temp_addr_l = 0;
    struct rt_i2c_msg msgs[2] = {0};
    struct AT24xxx_eeprom_device_t *eeprom = RT_NULL;
    struct AT24xxx_eeprom_config_t *config = RT_NULL;
    struct rt_i2c_bus_device *bus = RT_NULL;

    RT_ASSERT(dev);

    eeprom = (struct AT24xxx_eeprom_device_t *)dev;
    config = eeprom->config;
    bus = eeprom->bus;

    RT_ASSERT((pos + size) <= config->size);

    while(temp_size < size)
    {
        // 计算起始地址和可写入长度
        r_addr = pos + temp_size;
        r_size = (config->page_size - (r_addr % config->page_size)) > (size - temp_size)? (size - temp_size):(config->page_size - (r_addr % config->page_size));
        temp_addr_l = r_addr % 256;
        // 写入数据
        msgs[0].addr = (config->device_addr >> 1) + (r_addr / 256);
        msgs[0].flags |= RT_I2C_WR;
        msgs[0].len = 1;
        msgs[0].buf = &temp_addr_l;
        // ---
        msgs[1].addr = (config->device_addr >> 1) + (r_addr / 256);
        msgs[1].flags |= (RT_I2C_WR | RT_I2C_NO_START);
        msgs[1].len = r_size;
        msgs[1].buf = (rt_uint8_t *)buffer + temp_size;
        //更新数据
        r_addr += r_size;
        temp_size += r_size;
        
        if((rlt = rt_i2c_transfer(bus, msgs, 2)) < 0)
        {
            return rlt; 
        }
    }

    return size;
}

rt_err_t AT24xxx_eeprom_register(struct AT24xxx_eeprom_device_t *eeprom,
                                 struct AT24xxx_eeprom_config_t *config,
                                 struct rt_i2c_bus_device *bus,
                                 char *name)
{
    struct rt_device *device;

    RT_ASSERT(eeprom);
    RT_ASSERT(config);
    RT_ASSERT(bus);

    device = &eeprom->parent;

    eeprom->config = config;
    eeprom->bus = bus;

    device->type = RT_Device_Class_Block; 
    device->init = RT_NULL;
    device->open = RT_NULL;
    device->close = RT_NULL;
    device->read = AT24xxx_eeprom_read;
    device->write = AT24xxx_eeprom_write;
    device->control = RT_NULL;

    rt_device_register(&eeprom->parent, name, RT_DEVICE_FLAG_RDWR);

    return RT_EOK;
}

static struct AT24xxx_eeprom_config_t eeprom_config =
{
  .bus = "i2c1",
  .size = 256,
  .page_size = 8,
  .device_addr = 0xA0,
};

static struct AT24xxx_eeprom_device_t eeprom_obj;

int eeprom_init(void)
{
    return AT24xxx_eeprom_register(&eeprom_obj, &eeprom_config, (struct rt_i2c_bus_device *)rt_device_find(eeprom_config.bus), "AT24C02");
}
INIT_BOARD_EXPORT(eeprom_init);


static void _eeprom_cmd(int argc, char *argv[])
{
#define BUFFER_SIZE     10
    rt_uint8_t data[BUFFER_SIZE] = {0};
    
    if (0 == rt_strcmp("read", argv[1]))
    {
        rt_memset(data, 0x00 ,BUFFER_SIZE);
        if(AT24xxx_eeprom_read(&eeprom_obj.parent, 0, &data, BUFFER_SIZE) > 0)
        {
          LOG_D("eeprom read success");
          for (rt_uint8_t i = 0; i < BUFFER_SIZE; i++)
          {
            LOG_D("%d:0x%x", i, data[i]);
          }
        }
        else
        {
           LOG_D("eeprom read failed");
        }
    }
    else if (0 == rt_strcmp("write", argv[1]))
    {
        rt_memset(data, 0xA5 ,BUFFER_SIZE);
        if(AT24xxx_eeprom_write(&eeprom_obj.parent, 0, &data, BUFFER_SIZE) > 0)
        {
           LOG_D("eeprom write success");
        }
        else
        {
           LOG_D("eeprom write failed");
        }
    }
}
MSH_CMD_EXPORT_ALIAS(_eeprom_cmd, eeprom, eeprom [option]);
