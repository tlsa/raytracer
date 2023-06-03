
#include <math.h>
#include <float.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include <cgif.h>

#define ARRAY_LEN(_a) ((sizeof(_a)) / (sizeof(_a[0])))

#define COLOUR_COUNT 4

struct image {
	uint8_t *data;
	size_t width;
	size_t height;
};

static bool gif_add_frame(
		CGIF *gif,
		const struct image *img)
{
	CGIF_FrameConfig config = {
		.pImageData = img->data,
		.genFlags = CGIF_FRAME_GEN_USE_DIFF_WINDOW,
	};

	if (cgif_addframe(gif, &config) != CGIF_OK) {
		fprintf(stderr, "Error adding GIF frame\n");
		return false;
	}

	return true;
}

static CGIF *gif_create(
		const char *path,
		const struct image *img)
{
	static uint8_t palette[3 * COLOUR_COUNT] = {
		  0,   0,   0,
		255,   0,   0,
		255, 255,   0,
		255, 255, 255,
	};
	uint16_t width;
	uint16_t height;
	uint16_t palette_count = COLOUR_COUNT;

	if (img->width > UINT16_MAX ||
	    img->height > UINT16_MAX) {
		return NULL;
	}

	width = (uint16_t)img->width;
	height = (uint16_t)img->height;

	return cgif_newgif(&(CGIF_Config) {
		.path = path,
		.numLoops = 1,
		.attrFlags = 0,
		.width = width,
		.height = height,
		.pGlobalPalette = palette,
		.numGlobalPaletteEntries = palette_count,
	});
}

static void image_free(struct image *img)
{
	if (img != NULL) {
		free(img->data);
		free(img);
	}
}

static struct image *image_create(size_t width, size_t height)
{
	struct image *img;

	img = calloc(1, sizeof(*img));
	if (img == NULL) {
		return NULL;
	}

	img->data = calloc(width * height, sizeof(*img->data));
	if (img->data == NULL) {
		image_free(img);
		return NULL;
	}

	img->width = width;
	img->height = height;

	return img;
}

static inline void image_set_pixel(
		struct image *img,
		size_t x,
		size_t y,
		uint8_t colour)
{
	img->data[y * img->width + x] = colour & 0x3;
}

static inline int dither(size_t x, size_t y)
{
	static const int data[16] = {
		 0, 24,  6, 30,
		36, 12, 42, 18,
		 9, 33,  3, 27,
		45, 21, 39, 15,
	};
	return data[(y % 4) + (4 * (x % 4))];
}

typedef struct {
	double x;
	double y;
	double z;
} vector_t;

static inline vector_t vector_sub(
		const vector_t *vec, const vector_t *offset)
{
	vector_t sum = {
		.x = vec->x - offset->x,
		.y = vec->y - offset->y,
		.z = vec->z - offset->z,
	};

	return sum;
}

static inline vector_t vector_div(
		const vector_t *vec, double n)
{
	vector_t sum = {
		.x = vec->x / n,
		.y = vec->y / n,
		.z = vec->z / n,
	};

	return sum;
}

static inline double vector_dot_product(
		const vector_t *v1,
		const vector_t *v2)
{
	return v1->x * v2->x +
	       v1->y * v2->y +
	       v1->z * v2->z;
}

static inline double vector_magnitude_squared(
		const vector_t *vec)
{
	return vector_dot_product(vec, vec);
}

static inline vector_t vector_reflect(
		const vector_t *vec,
		const vector_t *normal)
{
	double p = 2 * vector_dot_product(vec, normal);
	vector_t ret = {
		.x = vec->x - p * normal->x,
		.y = vec->y - p * normal->y,
		.z = vec->z - p * normal->z,
	};

	return ret;
}

typedef struct {
	vector_t origin;
	vector_t direction;
} ray_t;

typedef struct {
	vector_t position;
	double radius;
} sphere_t;

static inline bool sphere_intersect(
		const sphere_t *sphere,
		const ray_t *ray,
		double *distance_out)
{
	vector_t v = vector_sub(&ray->origin, &sphere->position);
	double a = vector_magnitude_squared(&ray->direction);
	double b = 2.0 * vector_dot_product(&v, &ray->direction);
	double c = vector_magnitude_squared(&v) - sphere->radius * sphere->radius;
	double sqrt_discriminant;
	double discriminant;

	discriminant = b * b - 4.0 * a * c;
	if (discriminant < 0) {
		/* Missed sphere. */
		return false;
	}

	sqrt_discriminant = sqrt(discriminant);
	*distance_out = fmin((-b - sqrt_discriminant) / (2.0 * a),
	                     (-b + sqrt_discriminant) / (2.0 * a));
	return true;
}

static inline void sphere_reflect(
		const sphere_t *sphere,
		double distance,
		ray_t *ray)
{
	vector_t normal;

	ray->origin.x += distance * ray->direction.x;
	ray->origin.y += distance * ray->direction.y;
	ray->origin.z += distance * ray->direction.z;

	normal = vector_sub(&ray->origin, &sphere->position);
	normal = vector_div(&normal, sphere->radius);
	ray->direction = vector_reflect(&ray->direction, &normal);
}

static uint8_t get_colour(size_t x, size_t y, double raw_value)
{
	uint8_t value;

	assert(raw_value >= 0);
	assert(raw_value <= 1);

	value = ((48 * sqrt(raw_value)) + dither(x, y) / 3) / 16;

	assert(value < COLOUR_COUNT);

	return (COLOUR_COUNT - 1) - value;
}

int main(void)
{
	size_t width = 1600;
	size_t height = 900;
	size_t scale = (width + height) / 4;
	struct image *img;
	CGIF *gif;
	int ret;

	static const sphere_t spheres[] = {
		{
			.position = {
				.x = -1,
				.y =  1,
				.z =  0,
			},
			.radius = 1,
		},
		{
			.position = {
				.x =  1,
				.y = -1,
				.z =  0,
			},
			.radius = 1,
		},
		{
			.position = {
				.x =  4.5,
				.y =  3,
				.z = -4,
			},
			.radius = 5,
		},
		{
			.position = {
				.x = -0.5,
				.y = -0.5,
				.z =  1.75,
			},
			.radius = 0.6,
		},
		{
			.position = {
				.x = -10,
				.y =  2,
				.z = -8,
			},
			.radius = 2,
		},
	};

	img = image_create(width, height);
	if (img == NULL) {
		return EXIT_FAILURE;
	}

	gif = gif_create("out.gif", img);
	if (gif == NULL) {
		image_free(img);
		return EXIT_FAILURE;
	}

	for (size_t ypos = 0; ypos < height; ypos++) {
		for (size_t xpos = 0; xpos < width; xpos++) {
			ray_t ray = {
				.origin = {
					.x =  0,
					.y = -0.1,
					.z =  3,
				},
				.direction = {
					.x = ((double)xpos - (width / 2) - 0.5) / scale,
					.y = ((double)ypos - (height / 2) - 0.5) / scale,
					.z = 0,
				},
			};
			ray.direction.z = -1 / sqrt(1 + (ray.direction.x * ray.direction.x) + (ray.direction.y * ray.direction.y));

			ray.direction.x *= ray.direction.z;
			ray.direction.y *= ray.direction.z;

			while (true) {
				const sphere_t *closest = NULL;
				double best_distance = DBL_MAX;

				for (size_t i = 0; i < ARRAY_LEN(spheres); i++) {
					double distance;
					if (!sphere_intersect(&spheres[i], &ray, &distance)) {
						continue;
					}

					if (distance <= 0) {
						continue;
					}

					if (best_distance > distance) {
						best_distance = distance;
						closest = &spheres[i];
					}
				}

				if (closest == NULL) {
					break;
				}

				sphere_reflect(closest, best_distance, &ray);
			}

			if (ray.direction.y < 0) {
				/* Hit the floor. */
				double p = (ray.origin.y + 2) / ray.direction.y;
				int x = floor(ray.origin.x - (ray.direction.x * p));
				int z = floor(ray.origin.z - (ray.direction.z * p));
				ray.direction.y = 0.2 - ray.direction.y * (0.3 + (((x + z) & 1) / 2.0));
			}

			image_set_pixel(img, xpos, ypos, get_colour(xpos, ypos, ray.direction.y));
		}
	}

	if (!gif_add_frame(gif, img)) {
		ret = EXIT_FAILURE;
		goto exit;
	}

	ret = EXIT_SUCCESS;

exit:
	cgif_close(gif);
	image_free(img);

	return ret;
}
