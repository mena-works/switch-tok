#pragma once

#include <memory>
#include <vector>

#include <borealis.hpp>

#include "feed_item.hpp"

class VideoView;

class FeedActivity : public brls::Activity
{
  public:
    FeedActivity();
    ~FeedActivity() override;

    brls::View* createContentView() override;

    void openSearch();
    void cycleRegion();
    void openLogin();
    void restart();  // wipes the list and reloads from the top

  private:
    // append=false replaces the list (first load), append=true grows it.
    void loadFeed(bool append);
    void showIndex(int next);

    // Downloads the clips just ahead and drops everything behind.
    void primeCache();

    // Region feed, or search results when a query is set.
    std::string feedUrl() const;


    std::string region;
    std::string query;

    std::vector<FeedItem> items;
    int        index   = 0;
    bool       loading = false;
    VideoView* video   = nullptr;

    // Lets an in-flight fetch notice that the activity is gone.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
