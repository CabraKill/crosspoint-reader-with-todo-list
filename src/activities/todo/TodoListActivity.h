#pragma once

#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct TodoItem {
  std::string name;
  bool done;
};

class TodoListActivity final : public Activity {
 public:
  explicit TodoListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("TodoList", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  // Loads todos from storage; also used by SleepActivity for the todo sleep screen.
  static bool loadTodos(std::vector<TodoItem>& todos);

 private:
  ButtonNavigator buttonNavigator;
  std::vector<TodoItem> todos;
  int selectorIndex = 0;  // 0 = QR icon row, 1..N = todo items

  int getTotalItems() const;
  void onShowQrCode();
};
