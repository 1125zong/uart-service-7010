#include <stdio.h>
#include <string.h>

// 获取网卡：速率、双工、连接状态
int get_net_info(const char *ifname, int *speed, int *duplex, int *link_up)
{
    char path[256], buf[32];
    FILE *fp;

    // 1. 速率
    sprintf(path, "/sys/class/net/%s/speed", ifname);
    fp = fopen(path, "r");
    if (!fp) return -1;
    fscanf(fp, "%d", speed);
    fclose(fp);

    // 2. 双工 full=1, half=0
    sprintf(path, "/sys/class/net/%s/duplex", ifname);
    fp = fopen(path, "r");
    if (!fp) return -1;
    fscanf(fp, "%s", buf);
    *duplex = !strcmp(buf, "full");
    fclose(fp);

    // 3. 连接状态 up=1, down=0
    sprintf(path, "/sys/class/net/%s/operstate", ifname);
    fp = fopen(path, "r");
    if (!fp) return -1;
    fscanf(fp, "%s", buf);
    *link_up = !strcmp(buf, "up");
    fclose(fp);

    return 0;
}

int main()
{
    int speed, duplex, link_up;

    // 把 eth0 换成你的网卡名
    if (get_net_info("eth0", &speed, &duplex, &link_up) == 0)
    {
        printf("连接状态：%s\n", link_up ? "已连接 (UP)" : "未连接 (DOWN)");
        printf("速率：%d Mbps\n", speed);
        printf("双工：%s\n", duplex ? "全双工" : "半双工");

        if (link_up) {
            if (speed == 1000 && duplex)
                printf("✅ 千兆全双工\n");
            else if (speed == 100 && duplex)
                printf("✅ 百兆全双工\n");
        }
    }
    return 0;
}