#pragma once

#include <borealis.hpp>
#include "feed_activity.hpp"

class MenuActivity : public brls::Activity
{
  public:
    MenuActivity(FeedActivity* feed);
    ~MenuActivity() override = default;

    brls::View* createContentView() override;

  private:
    FeedActivity* feed;
};
