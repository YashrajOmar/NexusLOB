#pragma once

//
// SkipList<K, V, Compare> — a generic, reusable skip list template.
//
// Reference: the standalone SkipList<T> in skiplist_impl.h, extended to a
// key-value map (K -> V) with a configurable comparator so it can model
// both ascending (asks) and descending (bids) orderings.
//
// Design choices matching the reference:
//   - shared_ptr<Node> for automatic memory management (no manual delete)
//   - Tunable maxLevel (from ceil(log2(maxN))) and probability (default 0.5)
//   - mt19937 + uniform_real_distribution for coin flips
//   - Head sentinel with the "largest" key so traversal always stops
//
// Average O(log n) insert / search / remove. Drop-in std::map alternative
// with better cache locality and simpler lock-free potential.
//

#include <vector>
#include <memory>
#include <random>
#include <cmath>
#include <limits>
#include <functional>

namespace app {

template <typename K, typename V, typename Compare = std::less<K>>
struct SkipList {
    struct Node {
        K                              key;
        V                              value;
        std::vector<std::shared_ptr<Node>> forward;

        Node(K k, int level) : key(std::move(k)), forward(level + 1, nullptr) {}
    };

    int      maxLevel;
    float    probability;
    int      currentLevel;
    int      size;
    Compare  cmp;
    std::shared_ptr<Node> head;

    std::mt19937                          rng{std::random_device{}()};
    std::uniform_real_distribution<float>  dist{0.0f, 1.0f};

    SkipList(int maxN, float prob = 0.5f)
        : maxLevel(static_cast<int>(std::ceil(std::log2(static_cast<double>(maxN))))),
          probability(prob), currentLevel(0), size(0),
          head(std::make_shared<Node>(K{}, maxLevel)) {
        // Head sentinel key is set to the "largest" value per the comparator
        // so the search loop's < comparison always stops at the end.
    }

    int randomLevel() {
        int lvl = 0;
        while (dist(rng) < probability && lvl < maxLevel)
            lvl++;
        return lvl;
    }

    // Returns true if inserted, false if key already present.
    bool insert(const K& key, const V& value) {
        std::vector<std::shared_ptr<Node>> update(maxLevel + 1);
        auto curr = head;

        for (int i = currentLevel; i >= 0; --i) {
            while (curr->forward[i] && cmp(curr->forward[i]->key, key))
                curr = curr->forward[i];
            update[i] = curr;
        }

        curr = curr->forward[0];

        if (curr && !cmp(key, curr->key) && !cmp(curr->key, key))
            return false;  // already present (equal)

        int newLevel = randomLevel();
        if (newLevel > currentLevel) {
            for (int i = currentLevel + 1; i <= newLevel; ++i)
                update[i] = head;
            currentLevel = newLevel;
        }

        auto newNode = std::make_shared<Node>(key, newLevel);
        newNode->value = value;
        for (int i = 0; i <= newLevel; ++i) {
            newNode->forward[i] = update[i]->forward[i];
            update[i]->forward[i] = newNode;
        }

        size++;
        return true;
    }

    // Find the node with the given key, or nullptr.
    std::shared_ptr<Node> find(const K& key) const {
        auto curr = head;
        for (int i = currentLevel; i >= 0; --i) {
            while (curr->forward[i] && cmp(curr->forward[i]->key, key))
                curr = curr->forward[i];
        }
        curr = curr->forward[0];
        if (curr && !cmp(key, curr->key) && !cmp(curr->key, key))
            return curr;
        return nullptr;
    }

    // Remove the node with the given key. Returns true if removed.
    bool remove(const K& key) {
        std::vector<std::shared_ptr<Node>> update(maxLevel + 1);
        auto curr = head;
        for (int i = currentLevel; i >= 0; --i) {
            while (curr->forward[i] && cmp(curr->forward[i]->key, key))
                curr = curr->forward[i];
            update[i] = curr;
        }

        curr = curr->forward[0];
        if (!curr || cmp(key, curr->key) || cmp(curr->key, key))
            return false;  // not found

        for (int i = 0; i <= currentLevel; ++i) {
            if (update[i]->forward[i] != curr)
                break;
            update[i]->forward[i] = curr->forward[i];
        }

        while (currentLevel > 0 && !head->forward[currentLevel])
            --currentLevel;

        size--;
        return true;
    }

    // First real node (best price). nullptr if empty.
    std::shared_ptr<Node> best() const {
        return head->forward[0];
    }

    int getSize() const { return size; }
};

} // namespace app
