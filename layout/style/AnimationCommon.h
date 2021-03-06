/* vim: set shiftwidth=2 tabstop=8 autoindent cindent expandtab: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_css_AnimationCommon_h
#define mozilla_css_AnimationCommon_h

#include "nsIStyleRuleProcessor.h"
#include "nsIStyleRule.h"
#include "nsRefreshDriver.h"
#include "prclist.h"
#include "nsCSSProperty.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/StyleAnimationValue.h"
#include "mozilla/dom/AnimationTimeline.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Nullable.h"
#include "nsSMILKeySpline.h"
#include "nsStyleStruct.h"
#include "mozilla/Attributes.h"
#include "mozilla/FloatingPoint.h"
#include "nsCSSPseudoElements.h"
#include "nsCycleCollectionParticipant.h"

class nsIFrame;
class nsPresContext;
class nsStyleChangeList;

// X11 has a #define for CurrentTime.
#ifdef CurrentTime
#undef CurrentTime
#endif

namespace mozilla {

class RestyleTracker;
class StyleAnimationValue;
struct ElementPropertyTransition;
struct ElementAnimationCollection;

namespace css {

bool IsGeometricProperty(nsCSSProperty aProperty);

class CommonAnimationManager : public nsIStyleRuleProcessor,
                               public nsARefreshObserver {
public:
  CommonAnimationManager(nsPresContext *aPresContext);

  // nsISupports
  NS_DECL_ISUPPORTS

  // nsIStyleRuleProcessor (parts)
  virtual nsRestyleHint HasStateDependentStyle(StateRuleProcessorData* aData) MOZ_OVERRIDE;
  virtual nsRestyleHint HasStateDependentStyle(PseudoElementStateRuleProcessorData* aData) MOZ_OVERRIDE;
  virtual bool HasDocumentStateDependentStyle(StateRuleProcessorData* aData) MOZ_OVERRIDE;
  virtual nsRestyleHint
    HasAttributeDependentStyle(AttributeRuleProcessorData* aData) MOZ_OVERRIDE;
  virtual bool MediumFeaturesChanged(nsPresContext* aPresContext) MOZ_OVERRIDE;
  virtual size_t SizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf)
    const MOZ_MUST_OVERRIDE MOZ_OVERRIDE;
  virtual size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf)
    const MOZ_MUST_OVERRIDE MOZ_OVERRIDE;

  /**
   * Notify the manager that the pres context is going away.
   */
  void Disconnect();

  // Tell the restyle tracker about all the styles that we're currently
  // animating, so that it can update the animation rule for these
  // elements.
  void AddStyleUpdatesTo(mozilla::RestyleTracker& aTracker);

  enum FlushFlags {
    Can_Throttle,
    Cannot_Throttle
  };

  static bool ExtractComputedValueForTransition(
                  nsCSSProperty aProperty,
                  nsStyleContext* aStyleContext,
                  mozilla::StyleAnimationValue& aComputedValue);
protected:
  virtual ~CommonAnimationManager();

  // For ElementCollectionRemoved
  friend struct mozilla::ElementAnimationCollection;

  virtual void
  AddElementCollection(ElementAnimationCollection* aCollection) = 0;
  virtual void ElementCollectionRemoved() = 0;
  void RemoveAllElementCollections();

  // When this returns a value other than nullptr, it also,
  // as a side-effect, notifies the ActiveLayerTracker.
  static ElementAnimationCollection*
  GetAnimationsForCompositor(nsIContent* aContent,
                             nsIAtom* aElementProperty,
                             nsCSSProperty aProperty);

  PRCList mElementCollections;
  nsPresContext *mPresContext; // weak (non-null from ctor to Disconnect)
};

/**
 * A style rule that maps property-StyleAnimationValue pairs.
 */
class AnimValuesStyleRule MOZ_FINAL : public nsIStyleRule
{
public:
  // nsISupports implementation
  NS_DECL_ISUPPORTS

  // nsIStyleRule implementation
  virtual void MapRuleInfoInto(nsRuleData* aRuleData) MOZ_OVERRIDE;
#ifdef DEBUG
  virtual void List(FILE* out = stdout, int32_t aIndent = 0) const MOZ_OVERRIDE;
#endif

  void AddValue(nsCSSProperty aProperty,
                mozilla::StyleAnimationValue &aStartValue)
  {
    PropertyValuePair v = { aProperty, aStartValue };
    mPropertyValuePairs.AppendElement(v);
  }

  // Caller must fill in returned value.
  mozilla::StyleAnimationValue* AddEmptyValue(nsCSSProperty aProperty)
  {
    PropertyValuePair *p = mPropertyValuePairs.AppendElement();
    p->mProperty = aProperty;
    return &p->mValue;
  }

  struct PropertyValuePair {
    nsCSSProperty mProperty;
    mozilla::StyleAnimationValue mValue;
  };

private:
  ~AnimValuesStyleRule() {}

  InfallibleTArray<PropertyValuePair> mPropertyValuePairs;
};

class ComputedTimingFunction {
public:
  typedef nsTimingFunction::Type Type;
  void Init(const nsTimingFunction &aFunction);
  double GetValue(double aPortion) const;
  const nsSMILKeySpline* GetFunction() const {
    NS_ASSERTION(mType == nsTimingFunction::Function, "Type mismatch");
    return &mTimingFunction;
  }
  Type GetType() const { return mType; }
  uint32_t GetSteps() const { return mSteps; }
private:
  Type mType;
  nsSMILKeySpline mTimingFunction;
  uint32_t mSteps;
};

} /* end css sub-namespace */

struct AnimationPropertySegment
{
  float mFromKey, mToKey;
  mozilla::StyleAnimationValue mFromValue, mToValue;
  mozilla::css::ComputedTimingFunction mTimingFunction;
};

struct AnimationProperty
{
  nsCSSProperty mProperty;
  InfallibleTArray<AnimationPropertySegment> mSegments;
};

/**
 * Input timing parameters.
 *
 * Eventually this will represent all the input timing parameters specified
 * by content but for now it encapsulates just the subset of those
 * parameters passed to GetPositionInIteration.
 */
struct AnimationTiming
{
  mozilla::TimeDuration mIterationDuration;
  mozilla::TimeDuration mDelay;
  float mIterationCount; // mozilla::PositiveInfinity<float>() means infinite
  uint8_t mDirection;
  uint8_t mFillMode;

  bool FillsForwards() const {
    return mFillMode == NS_STYLE_ANIMATION_FILL_MODE_BOTH ||
           mFillMode == NS_STYLE_ANIMATION_FILL_MODE_FORWARDS;
  }
  bool FillsBackwards() const {
    return mFillMode == NS_STYLE_ANIMATION_FILL_MODE_BOTH ||
           mFillMode == NS_STYLE_ANIMATION_FILL_MODE_BACKWARDS;
  }
};

/**
 * Stores the results of calculating the timing properties of an animation
 * at a given sample time.
 */
struct ComputedTiming
{
  ComputedTiming()
  : mTimeFraction(kNullTimeFraction)
  , mCurrentIteration(0)
  , mPhase(AnimationPhase_Null)
  { }

  static const double kNullTimeFraction;

  // The total duration of the animation including all iterations.
  // Will equal TimeDuration::Forever() if the animation repeats indefinitely.
  TimeDuration mActiveDuration;

  // Will be kNullTimeFraction if the animation is neither animating nor
  // filling at the sampled time.
  double mTimeFraction;

  // Zero-based iteration index (meaningless if mTimeFraction is
  // kNullTimeFraction).
  uint64_t mCurrentIteration;

  enum {
    // Not sampled (null sample time)
    AnimationPhase_Null,
    // Sampled prior to the start of the active interval
    AnimationPhase_Before,
    // Sampled within the active interval
    AnimationPhase_Active,
    // Sampled after (or at) the end of the active interval
    AnimationPhase_After
  } mPhase;
};

/**
 * Data about one animation (i.e., one of the values of
 * 'animation-name') running on an element.
 */
class ElementAnimation : public nsWrapperCache
{
protected:
  virtual ~ElementAnimation() { }

public:
  explicit ElementAnimation(dom::AnimationTimeline* aTimeline)
    : mIsRunningOnCompositor(false)
    , mIsFinishedTransition(false)
    , mLastNotification(LAST_NOTIFICATION_NONE)
    , mTimeline(aTimeline)
  {
    SetIsDOMBinding();
  }

  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(ElementAnimation)
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_NATIVE_CLASS(ElementAnimation)

  dom::AnimationTimeline* GetParentObject() const { return mTimeline; }
  virtual JSObject* WrapObject(JSContext* aCx) MOZ_OVERRIDE;

  // AnimationPlayer methods
  dom::AnimationTimeline* Timeline() const { return mTimeline; }
  double StartTime() const;
  double CurrentTime() const;

  // FIXME: If we succeed in moving transition-specific code to a type of
  // AnimationEffect (as per the Web Animations API) we should remove these
  // virtual methods.
  virtual ElementPropertyTransition* AsTransition() { return nullptr; }
  virtual const ElementPropertyTransition* AsTransition() const {
    return nullptr;
  }

  bool IsPaused() const {
    return mPlayState == NS_STYLE_ANIMATION_PLAY_STATE_PAUSED;
  }

  // After transitions finish they need to be retained for one throttle-able
  // cycle (for reasons see explanation in nsTransitionManager.cpp). In the
  // meantime, however, they should be ignored.
  bool IsFinishedTransition() const {
    return mIsFinishedTransition;
  }
  void SetFinishedTransition() {
    MOZ_ASSERT(AsTransition(),
               "Calling SetFinishedTransition but it's not a transition");
    mIsFinishedTransition = true;
  }

  bool HasAnimationOfProperty(nsCSSProperty aProperty) const;
  bool IsRunning() const;
  bool IsCurrent() const;

  // Return the duration since the start of the delay period, taking into
  // account the pause state.  May be negative.
  // Returns a null value if the timeline associated with this object has a
  // current timestamp that is null or if the start time of this object is
  // null.
  Nullable<mozilla::TimeDuration> GetLocalTime() const {
    const mozilla::TimeStamp& timelineTime = mTimeline->GetCurrentTimeStamp();
    // FIXME: In order to support arbitrary timelines we will need to fix
    // the pause logic to handle the timeline time going backwards.
    MOZ_ASSERT(timelineTime.IsNull() || !IsPaused() ||
               timelineTime >= mPauseStart,
               "if paused, any non-null value of aTime must be at least"
               " mPauseStart");

    Nullable<mozilla::TimeDuration> result; // Initializes to null
    if (!timelineTime.IsNull() && !mStartTime.IsNull()) {
      result.SetValue((IsPaused() ? mPauseStart : timelineTime) - mStartTime);
    }
    return result;
  }

  // Return the duration from the start the active interval to the point where
  // the animation begins playback. This is zero unless the animation has
  // a negative delay in which case it is the absolute value of the delay.
  // This is used for setting the elapsedTime member of AnimationEvents.
  mozilla::TimeDuration InitialAdvance() const {
    return std::max(TimeDuration(), mTiming.mDelay * -1);
  }

  // This function takes as input the timing parameters of an animation and
  // returns the computed timing at the specified local time.
  //
  // The local time may be null in which case only static parameters such as the
  // active duration are calculated. All other members of the returned object
  // are given a null/initial value.
  //
  // This function returns ComputedTiming::kNullTimeFraction for the
  // mTimeFraction member of the return value if the animation should not be
  // run (because it is not currently active and is not filling at this time).
  static ComputedTiming
  GetComputedTimingAt(const Nullable<mozilla::TimeDuration>& aLocalTime,
                      const AnimationTiming& aTiming);

  // Shortcut for that gets the computed timing using the current local time as
  // calculated from the timeline time.
  ComputedTiming GetComputedTiming(const AnimationTiming& aTiming) const {
    return GetComputedTimingAt(GetLocalTime(), aTiming);
  }

  // Return the duration of the active interval for the given timing parameters.
  static mozilla::TimeDuration ActiveDuration(const AnimationTiming& aTiming);

  nsString mName;
  AnimationTiming mTiming;
  // The beginning of the delay period.
  mozilla::TimeStamp mStartTime;
  mozilla::TimeStamp mPauseStart;
  uint8_t mPlayState;
  bool mIsRunningOnCompositor;
  // A flag to mark transitions that have finished and are due to
  // be removed on the next throttle-able cycle.
  bool mIsFinishedTransition;

  enum {
    LAST_NOTIFICATION_NONE = uint64_t(-1),
    LAST_NOTIFICATION_END = uint64_t(-2)
  };
  // One of the above constants, or an integer for the iteration
  // whose start we last notified on.
  uint64_t mLastNotification;

  InfallibleTArray<AnimationProperty> mProperties;

  nsRefPtr<dom::AnimationTimeline> mTimeline;
};

typedef InfallibleTArray<nsRefPtr<ElementAnimation> > ElementAnimationPtrArray;

enum EnsureStyleRuleFlags {
  EnsureStyleRule_IsThrottled,
  EnsureStyleRule_IsNotThrottled
};

struct ElementAnimationCollection : public PRCList
{
  ElementAnimationCollection(dom::Element *aElement, nsIAtom *aElementProperty,
                             mozilla::css::CommonAnimationManager *aManager,
                             TimeStamp aNow)
    : mElement(aElement)
    , mElementProperty(aElementProperty)
    , mManager(aManager)
    , mAnimationGeneration(0)
    , mNeedsRefreshes(true)
#ifdef DEBUG
    , mCalledPropertyDtor(false)
#endif
  {
    MOZ_COUNT_CTOR(ElementAnimationCollection);
    PR_INIT_CLIST(this);
  }
  ~ElementAnimationCollection()
  {
    NS_ABORT_IF_FALSE(mCalledPropertyDtor,
                      "must call destructor through element property dtor");
    MOZ_COUNT_DTOR(ElementAnimationCollection);
    PR_REMOVE_LINK(this);
    mManager->ElementCollectionRemoved();
  }

  void Destroy()
  {
    // This will call our destructor.
    mElement->DeleteProperty(mElementProperty);
  }

  static void PropertyDtor(void *aObject, nsIAtom *aPropertyName,
                           void *aPropertyValue, void *aData);

  // This updates mNeedsRefreshes so the caller may need to check
  // for changes to values (for example, nsAnimationManager provides
  // CheckNeedsRefresh to register or unregister from observing the refresh
  // driver when this value changes).
  void EnsureStyleRuleFor(TimeStamp aRefreshTime, EnsureStyleRuleFlags aFlags);

  bool CanThrottleTransformChanges(mozilla::TimeStamp aTime);

  bool CanThrottleAnimation(mozilla::TimeStamp aTime);

  enum CanAnimateFlags {
    // Testing for width, height, top, right, bottom, or left.
    CanAnimate_HasGeometricProperty = 1,
    // Allow the case where OMTA is allowed in general, but not for the
    // specified property.
    CanAnimate_AllowPartial = 2
  };

  static bool
  CanAnimatePropertyOnCompositor(const dom::Element *aElement,
                                 nsCSSProperty aProperty,
                                 CanAnimateFlags aFlags);

  static bool IsCompositorAnimationDisabledForFrame(nsIFrame* aFrame);

  // True if this animation can be performed on the compositor thread.
  //
  // If aFlags contains CanAnimate_AllowPartial, returns whether the
  // state of this element's animations at the current refresh driver
  // time contains animation data that can be done on the compositor
  // thread.  (This is useful for determining whether a layer should be
  // active, or whether to send data to the layer.)
  //
  // If aFlags does not contain CanAnimate_AllowPartial, returns whether
  // the state of this element's animations at the current refresh driver
  // time can be fully represented by data sent to the compositor.
  // (This is useful for determining whether throttle the animation
  // (suppress main-thread style updates).)
  bool CanPerformOnCompositorThread(CanAnimateFlags aFlags) const;
  bool HasAnimationOfProperty(nsCSSProperty aProperty) const;

  bool IsForElement() const { // rather than for a pseudo-element
    return mElementProperty == nsGkAtoms::animationsProperty ||
           mElementProperty == nsGkAtoms::transitionsProperty;
  }

  bool IsForTransitions() const {
    return mElementProperty == nsGkAtoms::transitionsProperty ||
           mElementProperty == nsGkAtoms::transitionsOfBeforeProperty ||
           mElementProperty == nsGkAtoms::transitionsOfAfterProperty;
  }

  bool IsForAnimations() const {
    return mElementProperty == nsGkAtoms::animationsProperty ||
           mElementProperty == nsGkAtoms::animationsOfBeforeProperty ||
           mElementProperty == nsGkAtoms::animationsOfAfterProperty;
  }

  nsString PseudoElement()
  {
    if (IsForElement()) {
      return EmptyString();
    } else if (mElementProperty == nsGkAtoms::animationsOfBeforeProperty ||
               mElementProperty == nsGkAtoms::transitionsOfBeforeProperty) {
      return NS_LITERAL_STRING("::before");
    } else {
      return NS_LITERAL_STRING("::after");
    }
  }

  void PostRestyleForAnimation(nsPresContext *aPresContext) {
    nsRestyleHint styleHint = IsForElement() ? eRestyle_Self : eRestyle_Subtree;
    aPresContext->PresShell()->RestyleForAnimation(mElement, styleHint);
  }

  static void LogAsyncAnimationFailure(nsCString& aMessage,
                                       const nsIContent* aContent = nullptr);

  dom::Element *mElement;

  // the atom we use in mElement's prop table (must be a static atom,
  // i.e., in an atom list)
  nsIAtom *mElementProperty;

  mozilla::css::CommonAnimationManager *mManager;

  mozilla::ElementAnimationPtrArray mAnimations;

  // This style rule contains the style data for currently animating
  // values.  It only matches when styling with animation.  When we
  // style without animation, we need to not use it so that we can
  // detect any new changes; if necessary we restyle immediately
  // afterwards with animation.
  // NOTE: If we don't need to apply any styles, mStyleRule will be
  // null, but mStyleRuleRefreshTime will still be valid.
  nsRefPtr<mozilla::css::AnimValuesStyleRule> mStyleRule;

  // RestyleManager keeps track of the number of animation
  // 'mini-flushes' (see nsTransitionManager::UpdateAllThrottledStyles()).
  // mAnimationGeneration is the sequence number of the last flush where a
  // transition/animation changed.  We keep a similar count on the
  // corresponding layer so we can check that the layer is up to date with
  // the animation manager.
  uint64_t mAnimationGeneration;
  // Update mAnimationGeneration to nsCSSFrameConstructor's count
  void UpdateAnimationGeneration(nsPresContext* aPresContext);

  // Returns true if there is an animation in the before or active phase
  // at the current time.
  bool HasCurrentAnimations();

  // The refresh time associated with mStyleRule.
  TimeStamp mStyleRuleRefreshTime;

  // False when we know that our current style rule is valid
  // indefinitely into the future (because all of our animations are
  // either completed or paused).  May be invalidated by a style change.
  bool mNeedsRefreshes;

#ifdef DEBUG
  bool mCalledPropertyDtor;
#endif
};

}

#endif /* !defined(mozilla_css_AnimationCommon_h) */
