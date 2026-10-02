#pragma once
#include <cstdint>
#include <cstddef>
void irFavoritesMenu();
bool saveIrFavorite(const uint16_t *raw, size_t count, uint16_t hz, const char *suggestion, bool offOnly = false);
int sendIrOffFavorites();
