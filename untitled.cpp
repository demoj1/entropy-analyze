#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <span>
#include <stdio.h>
#include <raylib.h>
#include <string_view>
#include <thread>
#include <vector>
#include <cassert>
#include <unistd.h>
#include <stdio.h>
#include <cstdio>

using namespace std;

#define ENTROPY_WINDOW_SIZE 256
#define VIEW_BUFFER 2048

char buffer[VIEW_BUFFER];
Font global_font;

// template <size_t ChunkSize>
vector<float> get_entropy(vector<char>& buffer, map<char, float>& freq) {
  vector<float> out_vector = {};

  int chunks = ENTROPY_WINDOW_SIZE;
  chunks = chunks == 0 ? 1 : chunks;

  assert(chunks > 0);

  auto hx = 0.0;
  for (int i = 0; i < buffer.size(); i++) {
    hx -= freq[buffer[i]] * logb(freq[buffer[i]]);
    if (i % chunks == 0) {
      out_vector.push_back(hx);
      hx = 0.0;
    }
  }
  if (hx != 0) out_vector.push_back(hx);

  return std::move(out_vector);
}

struct AFile {
  vector<char> file_buffer;
  filesystem::path path;
  ifstream file_stream;
  map<char, float> freq;
  vector<float> entropy_buffer;
  float max_entropy;
  filesystem::file_time_type last_write_time;
  float chunk_size;

  int collide_idx = 0;

  bool render(Rectangle rect) {
    float width = rect.width / this->entropy_buffer.size();
    assert(width > 0);

    bool active = false;

    SetTextLineSpacing(27);

    auto mouse_xy = GetMousePosition();

    bool widget_collision = false;

    if (!CheckCollisionPointRec(mouse_xy, rect)) {
      this->collide_idx = -1;
    } else {
      widget_collision = true;
    }

    int chunk_i = -1, ibar = -1;
    for (auto hx : this->entropy_buffer) {
      chunk_i++; ibar++;

      Color color = {140, 71, 210, 255};

      auto x = rect.x + ibar * width;
      auto y = (rect.height + rect.y) - (1 - ((hx / this->max_entropy) * (float)0.99)) * rect.height;
      auto height = rect.y + rect.height - y;

      bool is_collision = CheckCollisionPointRec(mouse_xy, {
        x,
        rect.y,
        width,
        rect.height,
      });

      if (is_collision) this->collide_idx = chunk_i;

      if (this->collide_idx != -1
       && (chunk_i > this->collide_idx - (VIEW_BUFFER / chunk_size) / 2
        && chunk_i < this->collide_idx + (VIEW_BUFFER / chunk_size) / 2
       )) {
        color = {0, 255, 0, 150};
      }

      DrawRectangleRec({x, y, width, height}, color);

      if (is_collision) {
        stringstream s; s << "e: " << 1 - hx / this->max_entropy;
        DrawTextEx(global_font, s.str().c_str(), {mouse_xy.x + 15, mouse_xy.y}, (float)global_font.baseSize/2, 2, {255, 255, 0, 255});
      }

      if (is_collision && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        int pos_in_file = (float)chunk_i * ENTROPY_WINDOW_SIZE - VIEW_BUFFER/2;
        if (pos_in_file < 0) pos_in_file = 0;

        int end_position = pos_in_file + VIEW_BUFFER;
        end_position = end_position > this->file_buffer.size() ? this->file_buffer.size() : end_position;

        active = true;

        span<char> span(this->file_buffer.data() + pos_in_file, this->file_buffer.data() + end_position);

        DrawTextEx(global_font, string(span.data()).c_str(), { 0, rect.y + rect.height }, (float)global_font.baseSize, 2, {255, 255, 0, 255});
      }
    }

    if (widget_collision) {
      Color color = {255, 0, 0, 255};
      auto y = rect.y;
      auto height = rect.y + rect.height - y;
      DrawRectangleRec({mouse_xy.x, y, 1, height}, color);
    }

    DrawTextEx(global_font, this->path.c_str(), {rect.x, rect.y}, (float)global_font.baseSize, 2, {0, 255, 0, 255});
    return active;
  }

  void try_update(bool force = false) {
    auto ffile = std::filesystem::path(this->path);
    auto ffile_last_write = std::filesystem::last_write_time(ffile);
    if (ffile_last_write == this->last_write_time && force == false) return;

    auto freq_table = std::map<char, float>();
    this->file_stream = ifstream(this->path, ios_base::binary);

    size_t res = 0;
    vector<char> file_buffer;
    while (this->file_stream) {
      char c; this->file_stream >> std::noskipws >> c;
      file_buffer.push_back(c);
      freq_table[c]++;
    }

    for (auto &p : freq_table) p.second /= (float)file_buffer.size();
    auto entropy = get_entropy(file_buffer, freq_table);
    auto max_hx = *max_element(entropy.begin(), entropy.end());

    int chunk_size = filesystem::file_size(this->path) / ENTROPY_WINDOW_SIZE;
    chunk_size = chunk_size == 0 ? 1 : chunk_size;

    this->file_buffer = file_buffer;
    this->freq = freq_table;
    this->entropy_buffer = entropy;
    this->max_entropy = max_hx;
    this->last_write_time = ffile_last_write;
    this->chunk_size = chunk_size;
  }
};

int main() {
  map<filesystem::path, AFile> files = {};

  int width = 1024;
  int height = 1024;

  int scrollSpeed = 85;
  int boxPositionY = 0;

  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  SetTargetFPS(120);
  InitWindow(width, height, "Hello world!");

  float dt = 0;

  global_font = LoadFontEx("/home/dmr/Documents/PragmataProR_liga_0829.ttf", 24, 0, 1500);

  while (!WindowShouldClose()) {
    auto scroll = (GetMouseWheelMove()*scrollSpeed);

    boxPositionY += scroll*GetFrameTime()*50;
    if (boxPositionY > 0) boxPositionY = 0;

    height = GetScreenHeight();
    width = GetScreenWidth();

    if (IsFileDropped()) {
      auto file_path_list = LoadDroppedFiles();

      for (int i = 0; i < file_path_list.count; i++) {
        auto ffile = std::filesystem::path(file_path_list.paths[i]);
        auto ffile_last_write = std::filesystem::last_write_time(ffile);

        if (files.count(ffile) > 0)
          continue;

        files[ffile] = { .path = ffile };
        files[ffile].try_update(true);
      }

      UnloadDroppedFiles(file_path_list);
    }

    BeginDrawing();
      ClearBackground({0, 0, 0, 0});

        if (files.size() == 0) {
          EndDrawing();
          continue;
        }

        float y = 0.0;
        bool active = false;
        for (auto& it : files) {
          if (active) continue;
          active = it.second.render({
            0,
            (float)(y++ * 300 + boxPositionY),
            (float)width,
            300});
        }

      if (dt > 0.5) {
        dt = 0;
        vector<std::thread> threads;
        for (auto& it : files) threads.push_back(std::thread([&it]() {
          it.second.try_update();
        }));

        for (auto& thread : threads) thread.join();
      }

      dt += GetFrameTime();
    EndDrawing();
  }

  CloseWindow();
}