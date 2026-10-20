#pragma once

namespace eg13 {

// Last-note-priority stack of held keys. Fixed capacity, no allocation.
class MonoNoteStack {
 public:
  static constexpr int kCapacity = 16;

  void noteOn(int key, float velocity) {
    remove(key);
    if (size_ == kCapacity) removeAt(0);
    entries_[size_++] = {key, velocity};
  }

  void noteOff(int key) { remove(key); }
  void clear() { size_ = 0; }
  bool empty() const { return size_ == 0; }
  int size() const { return size_; }
  int topKey() const { return entries_[size_ - 1].key; }          // requires !empty()
  float topVelocity() const { return entries_[size_ - 1].velocity; }  // requires !empty()

 private:
  struct Entry {
    int key;
    float velocity;
  };

  void remove(int key) {
    for (int i = 0; i < size_; ++i) {
      if (entries_[i].key == key) {
        removeAt(i);
        return;
      }
    }
  }

  void removeAt(int index) {
    for (int i = index; i < size_ - 1; ++i) entries_[i] = entries_[i + 1];
    --size_;
  }

  Entry entries_[kCapacity] = {};
  int size_ = 0;
};

}  // namespace eg13
