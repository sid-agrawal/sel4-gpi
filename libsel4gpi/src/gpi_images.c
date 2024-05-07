#include <sel4gpi/gpi_images.h>
#include <string.h>

int sel4gpi_image_name_to_id(const char *image_name)
{
    int image_id = -1;
    for (int i = 0; i < PD_N_IMAGES; i++)
    {
        if (strcmp(image_name, pd_images[i]) == 0)
        {
            image_id = i;
            break;
        }
    }

    return image_id;
}