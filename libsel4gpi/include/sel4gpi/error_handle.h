#pragma once

#define GOTO_IF_ERR(err) \
    do                   \
    {                    \
        if (err)         \
        {                \
            goto error;  \
        }                \
    } while (0)

#define GOTO_IF_COND(c) \
    do                  \
    {                   \
        if ((c))        \
        {               \
            goto error; \
        }               \
    } while (0)

#define GOTO_PRINT_IF_COND(c, msg, ...) \
    do                                  \
    {                                   \
        if ((c))                        \
        {                               \
            printf(msg, ##__VA_ARGS__); \
            goto error;                 \
        }                               \
    } while (0)
