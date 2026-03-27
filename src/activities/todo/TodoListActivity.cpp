#include "TodoListActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <WiFi.h>

#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "activities/reader/QrDisplayActivity.h"
#include "components/UITheme.h"
#include "components/icons/todo.h"
#include "fontIds.h"

namespace {
// Todo list file path on SD card
constexpr char TODO_FILE_PATH[] = "/.crosspoint/todos.txt";
// QR code placeholder URL (device IP will be appended as ?ip=<ip>)
constexpr char TODO_QR_BASE_URL[] = "http://todo.crosspoint.local/";
// Icon size for the QR icon button
constexpr int QR_ICON_SIZE = 32;
// Padding used when drawing items
constexpr int ITEM_SIDE_PADDING = 20;
}  // namespace

bool TodoListActivity::loadTodos(std::vector<TodoItem>& todos) {
  todos.clear();

  FsFile file;
  if (!Storage.openFileForRead("TODO", TODO_FILE_PATH, file)) {
    return false;
  }

  char lineBuf[256];
  while (file.available()) {
    int len = 0;
    // Read one line (up to newline or buffer limit)
    while (file.available() && len < static_cast<int>(sizeof(lineBuf)) - 1) {
      char c = static_cast<char>(file.read());
      if (c == '\n') {
        break;
      }
      if (c != '\r') {
        lineBuf[len++] = c;
      }
    }
    lineBuf[len] = '\0';

    if (len == 0) {
      continue;
    }

    // Format: name|done  (done = '1' or '0')
    char* sep = strchr(lineBuf, '|');
    if (sep == nullptr) {
      continue;
    }
    *sep = '\0';
    const char* name = lineBuf;
    const char* doneStr = sep + 1;

    TodoItem item;
    item.name = name;
    item.done = (doneStr[0] == '1');
    todos.push_back(std::move(item));
  }

  file.close();
  return true;
}

int TodoListActivity::getTotalItems() const {
  // 1 for QR icon row + number of todo items
  return 1 + static_cast<int>(todos.size());
}

void TodoListActivity::onEnter() {
  Activity::onEnter();
  todos.clear();
  loadTodos(todos);
  selectorIndex = 0;
  requestUpdate();
}

void TodoListActivity::onExit() {
  Activity::onExit();
  todos.clear();
}

void TodoListActivity::loop() {
  const int total = getTotalItems();

  buttonNavigator.onNext([this, total] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, total);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, total] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, total);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goHome();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectorIndex == 0) {
      onShowQrCode();
    }
  }
}

void TodoListActivity::onShowQrCode() {
  // Build placeholder URL with device IP
  char ipStr[16] = "0.0.0.0";
  if (WiFi.status() == WL_CONNECTED) {
    const IPAddress ip = WiFi.localIP();
    snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  } else if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
    const IPAddress ip = WiFi.softAPIP();
    snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  }

  char url[128];
  snprintf(url, sizeof(url), "%s?ip=%s", TODO_QR_BASE_URL, ipStr);

  startActivityForResult(std::make_unique<QrDisplayActivity>(renderer, mappedInput, std::string(url)),
                         [this](const ActivityResult&) { requestUpdate(); });
}

void TodoListActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  // Draw header with title
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_TODO_LIST), nullptr);

  const int contentY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      pageHeight - contentY - metrics.buttonHintsHeight - metrics.verticalSpacing;

  // Row heights
  constexpr int qrRowHeight = 48;
  constexpr int itemRowHeight = 36;
  constexpr int rowSpacing = 4;

  // --- Draw QR icon row (index 0) ---
  {
    const bool selected = (selectorIndex == 0);
    const int rowY = contentY;
    const int rowX = ITEM_SIDE_PADDING;
    const int rowW = pageWidth - ITEM_SIDE_PADDING * 2;

    if (selected) {
      renderer.fillRect(rowX, rowY, rowW, qrRowHeight);
    } else {
      renderer.drawRect(rowX, rowY, rowW, qrRowHeight);
    }

    // Draw the todo icon centered vertically in the row
    const int iconY = rowY + (qrRowHeight - QR_ICON_SIZE) / 2;
    renderer.drawIcon(TodoIcon, rowX + 8, iconY, QR_ICON_SIZE, QR_ICON_SIZE);

    // QR hint text
    const char* hint = tr(STR_SHOW_QR_CODE);
    const int textY = rowY + (qrRowHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawText(UI_10_FONT_ID, rowX + QR_ICON_SIZE + 16, textY, hint, !selected);
  }

  // --- Draw todo items ---
  for (int i = 0; i < static_cast<int>(todos.size()); ++i) {
    const int itemIdx = i + 1;  // index 0 is QR icon
    const bool selected = (selectorIndex == itemIdx);
    const int rowY = contentY + qrRowHeight + rowSpacing + i * (itemRowHeight + rowSpacing);

    if (rowY + itemRowHeight > contentY + contentHeight) {
      break;  // No more vertical space
    }

    const int rowX = ITEM_SIDE_PADDING;
    const int rowW = pageWidth - ITEM_SIDE_PADDING * 2;

    if (selected) {
      renderer.fillRect(rowX, rowY, rowW, itemRowHeight);
    }

    // Checkbox: "[ ]" or "[x]"
    const char* checkbox = todos[i].done ? "[x]" : "[  ]";
    const int checkW = renderer.getTextWidth(UI_10_FONT_ID, checkbox);
    const int textY = rowY + (itemRowHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawText(UI_10_FONT_ID, rowX + 8, textY, checkbox, !selected);

    // Item name (truncated to fit)
    const int nameX = rowX + 8 + checkW + 8;
    const int nameMaxW = rowW - (nameX - rowX) - 8;
    const auto truncated = renderer.truncatedText(UI_10_FONT_ID, todos[i].name.c_str(), nameMaxW);
    renderer.drawText(UI_10_FONT_ID, nameX, textY, truncated.c_str(), !selected);
  }

  // Button hints
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
