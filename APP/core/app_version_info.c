#include <stdio.h>
#include <string.h>

#include "app_version.h"
#include "app_version_info.h"

/*
 * 版本标记实例：把版本号固化进本程序的只读数据区。
 * __attribute__((used)) 防止被优化/链接器回收；
 * 读文件时只需要在二进制中搜索 APP_VERSION_MARKER_PREFIX 即可。
 */
__attribute__((used, section(".rodata")))
const char g_app_version_marker[] = APP_VERSION_MARKER;

const char *app_version_running(void)
{
    return FW_VERSION_STRING;
}

int app_version_read_binary(const char *path, char *out, size_t out_len)
{
    FILE *fp;
    char buf[4096 + 64];
    size_t keep = 0;
    const size_t prefix_len = strlen(APP_VERSION_MARKER_PREFIX);

    if (path == NULL || out == NULL || out_len == 0)
    {
        return APP_VERSION_ERR_NO_FILE;
    }
    out[0] = '\0';

    fp = fopen(path, "rb");
    if (fp == NULL)
    {
        return APP_VERSION_ERR_NO_FILE;
    }

    for (;;)
    {
        size_t n = fread(buf + keep, 1, sizeof(buf) - keep, fp);
        size_t total = keep + n;
        size_t i;

        for (i = 0; i + prefix_len <= total; i++)
        {
            if (memcmp(buf + i, APP_VERSION_MARKER_PREFIX, prefix_len) == 0)
            {
                size_t j = i + prefix_len;
                size_t k = 0;

                while (j < total && (k + 1) < out_len &&
                       buf[j] >= 0x20 && buf[j] < 0x7f)
                {
                    out[k++] = buf[j++];
                }
                out[k] = '\0';

                fclose(fp);
                return (k > 0) ? APP_VERSION_OK : APP_VERSION_ERR_NO_MARKER;
            }
        }

        if (n == 0)
        {
            break;
        }

        /* 保留末尾 prefix_len 字节，避免标记正好被块边界截断 */
        keep = (total > prefix_len) ? prefix_len : total;
        memmove(buf, buf + total - keep, keep);
    }

    fclose(fp);
    return APP_VERSION_ERR_NO_MARKER;
}

/* 把读取结果转成便于阅读的描述，区分“文件不存在”和“旧二进制无标记” */
static void describe_binary_version(const char *path, const char *absent_text,
                                    char *desc, size_t desc_len)
{
    int rc = app_version_read_binary(path, desc, desc_len);

    if (rc == APP_VERSION_OK)
    {
        return;
    }

    if (rc == APP_VERSION_ERR_NO_FILE)
    {
        snprintf(desc, desc_len, "%s", absent_text);
    }
    else
    {
        snprintf(desc, desc_len, "unknown (no version marker)");
    }
}

void app_version_dump_all(const char *bin_path)
{
    char installed[64] = {0};

    if (bin_path == NULL)
    {
        bin_path = APP_INSTALL_BIN_PATH;
    }

    describe_binary_version(bin_path, "none (not installed)",
                            installed, sizeof(installed));

    printf("[VERSION] running : %s\n", app_version_running());
    printf("[VERSION] on-disk : %s (%s)\n", installed, bin_path);
}
