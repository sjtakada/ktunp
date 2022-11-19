/*
 * Kernel To User Notification Pipeline (KTUNP)
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/kobject.h>

#include <linux/inetdevice.h>
//#include <linux/inet.h>
#include <net/ip.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/nexthop.h>

static __be32 in_aton(const char *str)
{
        unsigned int l;
        unsigned int val;
        int count = 0;
        
        int i;

        l = 0;
        for (i = 0; i < 4; i++) {
                l <<= 8;
                if (*str != '\0') {
                        val = 0;
                        while (*str != '\0' && *str != '.' && *str != '\n') {
                                val *= 10;
                                val += *str - '0';
                                str++;
                        }
                        if (*str == '.')
                                count++;

                        l |= val;
                        if (*str != '\0')
                                str++;
                }
        }
        if (count != 3)
                return 0;

        return htonl(l);
}

static struct kobject *ktunpobj;

struct d_attr {
  struct attribute attr;
  int value;
  struct in_addr ip4addr;
};

static struct d_attr nexthop = {
  .attr.name="nexthop",
  .attr.mode = 0644,
  .value = 0,
  .ip4addr = {0},
};

static struct d_attr inetaddr = {
  .attr.name="inetaddr",
  .attr.mode = 0644,
  .value = NETDEV_DOWN,
  .ip4addr = {0},
};

static struct attribute * d_attrs[] = {
  &nexthop.attr,
  &inetaddr.attr,
  NULL
};

/*
 *  Nexthop tracking notifier.
 */
static unsigned int nht_net_id;

struct nht_net {
  struct notifier_block nexthop_notifier_block;
};

static int nht_nexthop_event(struct notifier_block *nb,
                             unsigned long event, void *ptr)
{
  struct nh_notifier_info *info = ptr;
  struct nexthop *nh;

  if (event != NEXTHOP_EVENT_DEL)
    return NOTIFY_DONE;

  nh = nexthop_find_by_id(info->net, info->id);
  if (!nh)
    return NOTIFY_DONE;

  sysfs_notify(ktunpobj, NULL, "nexthop");

  return NOTIFY_DONE;
}


static __net_init int nht_init_net(struct net *net)
{
  struct nht_net *nn = net_generic(net, nht_net_id);

  nn->nexthop_notifier_block.notifier_call = nht_nexthop_event;
  return register_nexthop_notifier(net, &nn->nexthop_notifier_block, NULL);
}

static void __net_exit nht_exit_batch_net(struct list_head *net_list)
{
  struct net *net;
  LIST_HEAD(list);

  list_for_each_entry(net, net_list, exit_list) {
    struct nht_net *nn = net_generic(net, nht_net_id);
    unregister_nexthop_notifier(net, &nn->nexthop_notifier_block);
  }
}

static struct pernet_operations nht_net_ops = {
  .init = nht_init_net,
  .exit_batch = nht_exit_batch_net,
  .id   = &nht_net_id,
  .size = sizeof(struct nht_net),
};

/*
 * InetAddr notifier. 
 */
static int nht_inetaddr_event(struct notifier_block *this, unsigned long ev,
                              void *ptr)
{
  struct in_ifaddr *if4 = (struct in_ifaddr *)ptr;

  if (ev == NETDEV_UP) {
    if (inetaddr.ip4addr.s_addr == if4->ifa_address) {
      sysfs_notify(ktunpobj, NULL, "inetaddr");
    }
  }

  return NOTIFY_DONE;
}

static struct notifier_block nht_inetaddr_notifier = {
  .notifier_call = nht_inetaddr_event,
};



/*
 * Notify to user through sysfs.
 */
static ssize_t show(struct kobject *kobj, struct attribute *attr, char *buf)
{
  struct d_attr *da = container_of(attr, struct d_attr, attr);

  pr_info("ktunp show called (%s)\n", da->attr.name);

  return scnprintf(buf, PAGE_SIZE, "%s: %u.%u.%u.%u\n", da->attr.name,
                   *((unchar *)&da->ip4addr),
                   *((unchar *)&da->ip4addr + 1),
                   *((unchar *)&da->ip4addr + 2),
                   *((unchar *)&da->ip4addr + 3));
}

static ssize_t store(struct kobject *kobj, struct attribute *attr, const char *buf, size_t len)
{
  struct d_attr *da = container_of(attr, struct d_attr, attr);
  uint32_t ip4addr;

  ip4addr = in_aton(buf);
  pr_info("len = %lu %s %u\n", len, buf, ip4addr);

  if (ip4addr) {
    if (strcmp(da->attr.name, "nexthop") == 0) {
      da->ip4addr.s_addr = ip4addr;
    } else if (strcmp(da->attr.name, "inetaddr") == 0) {
      da->ip4addr.s_addr = ip4addr;
    }
  }

  return sizeof(int);
}

static struct sysfs_ops s_ops = {
  .show = show,
  .store = store,
};

static struct kobj_type k_type = {
  .sysfs_ops = &s_ops,
  .default_attrs = d_attrs,
};

static int ktunp_sysfs_init(void)
{
  ktunpobj = kzalloc(sizeof(*ktunpobj), GFP_KERNEL);
  if (ktunpobj) {
    kobject_init(ktunpobj, &k_type);
    if (kobject_add(ktunpobj, NULL, "%s", "ktunp")) {
      pr_info("ktunp: kobject_add() failed\n");
      kobject_put(ktunpobj);
      ktunpobj = NULL;

      return -1;
    }
  }

  return 0;
}

/*
 * Module init and exit.
 */

static int __init ktunp_module_init(void)
{
  int rc;
  int err = -1;

  pr_info("ktunp module init\n");
  rc = ktunp_sysfs_init();
  if (rc) {
    goto out1;
  }

  rc = register_inetaddr_notifier(&nht_inetaddr_notifier);
  if (rc) {
    goto out2;
  }

  rc = register_pernet_subsys(&nht_net_ops);
  if (rc) {
    goto out3;
  }

  return 0;

 out3:
  unregister_inetaddr_notifier(&nht_inetaddr_notifier);

 out2:
 out1:
  return err;
}

static void __exit ktunp_module_exit(void)
{
  if (ktunpobj) {
    kobject_put(ktunpobj);
    kfree(ktunpobj);
  }
  unregister_pernet_subsys(&nht_net_ops);
  unregister_inetaddr_notifier(&nht_inetaddr_notifier);

  pr_info("ktunp module exit\n");
}

module_init(ktunp_module_init);
module_exit(ktunp_module_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Toshiaki Takada <toshiaki.takada@gmail.com>");
