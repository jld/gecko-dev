/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { PrefFlipsFeature } from "resource://nimbus/lib/PrefFlipsFeature.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ClientEnvironment: "resource://normandy/lib/ClientEnvironment.sys.mjs",
  ClientID: "resource://gre/modules/ClientID.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  ExperimentStore: "resource://nimbus/lib/ExperimentStore.sys.mjs",
  FirstStartup: "resource://gre/modules/FirstStartup.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  NimbusTelemetry: "resource://nimbus/lib/Telemetry.sys.mjs",
  NormandyUtils: "resource://normandy/lib/NormandyUtils.sys.mjs",
  PrefUtils: "resource://normandy/lib/PrefUtils.sys.mjs",
  EnrollmentsContext:
    "resource://nimbus/lib/RemoteSettingsExperimentLoader.sys.mjs",
  Sampling: "resource://gre/modules/components-utils/Sampling.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "log", () => {
  const { Logger } = ChromeUtils.importESModule(
    "resource://messaging-system/lib/Logger.sys.mjs"
  );
  return new Logger("ExperimentManager");
});

const TELEMETRY_DEFAULT_EXPERIMENT_TYPE = "nimbus";

const UPLOAD_ENABLED_PREF = "datareporting.healthreport.uploadEnabled";
const STUDIES_OPT_OUT_PREF = "app.shield.optoutstudies.enabled";

const STUDIES_ENABLED_CHANGED = "nimbus:studies-enabled-changed";

function featuresCompat(branch) {
  if (!branch || (!branch.feature && !branch.features)) {
    return [];
  }
  let { features } = branch;
  // In <=v1.5.0 of the Nimbus API, experiments had single feature
  if (!features) {
    features = [branch.feature];
  }

  return features;
}

function getFeatureFromBranch(branch, featureId) {
  return featuresCompat(branch).find(
    featureConfig => featureConfig.featureId === featureId
  );
}

/**
 * A module for processes Experiment recipes, choosing and storing enrollment state,
 * and sending experiment-related Telemetry.
 */
export class _ExperimentManager {
  constructor({ id = "experimentmanager", store } = {}) {
    this.id = id;
    this.store = store || new lazy.ExperimentStore();
    this.sessions = new Map();
    this.optInRecipes = [];
    // By default, no extra context.
    this.extraContext = {};
    Services.prefs.addObserver(UPLOAD_ENABLED_PREF, this);
    Services.prefs.addObserver(STUDIES_OPT_OUT_PREF, this);

    // A Map from pref names to pref observers and metadata. See
    // `_updatePrefObservers` for the full structure.
    this._prefs = new Map();
    // A Map from enrollment slugs to a Set of prefs that enrollment is setting
    // or would set (e.g., if the enrollment is a rollout and there wasn't an
    // active experiment already setting it).
    this._prefsBySlug = new Map();

    this._prefFlips = new PrefFlipsFeature({ manager: this });
  }

  get studiesEnabled() {
    return (
      Services.prefs.getBoolPref(UPLOAD_ENABLED_PREF, false) &&
      Services.prefs.getBoolPref(STUDIES_OPT_OUT_PREF, false) &&
      Services.policies.isAllowed("Shield")
    );
  }

  /**
   * Creates a targeting context with following filters:
   *
   *   * `activeExperiments`: an array of slugs of all the active experiments
   *   * `isFirstStartup`: a boolean indicating whether or not the current enrollment
   *      is performed during the first startup
   *
   * @returns {Object} A context object
   * @memberof _ExperimentManager
   */
  createTargetingContext() {
    let context = {
      ...this.extraContext,

      isFirstStartup: lazy.FirstStartup.state === lazy.FirstStartup.IN_PROGRESS,

      get currentDate() {
        return new Date();
      },
    };
    Object.defineProperty(context, "activeExperiments", {
      enumerable: true,
      get: async () => {
        await this.store.ready();
        return this.store.getAllActiveExperiments().map(exp => exp.slug);
      },
    });
    Object.defineProperty(context, "activeRollouts", {
      enumerable: true,
      get: async () => {
        await this.store.ready();
        return this.store.getAllActiveRollouts().map(rollout => rollout.slug);
      },
    });
    Object.defineProperty(context, "previousExperiments", {
      enumerable: true,
      get: async () => {
        await this.store.ready();
        return this.store
          .getAll()
          .filter(enrollment => !enrollment.active && !enrollment.isRollout)
          .map(exp => exp.slug);
      },
    });
    Object.defineProperty(context, "previousRollouts", {
      enumerable: true,
      get: async () => {
        await this.store.ready();
        return this.store
          .getAll()
          .filter(enrollment => !enrollment.active && enrollment.isRollout)
          .map(rollout => rollout.slug);
      },
    });
    Object.defineProperty(context, "enrollments", {
      enumerable: true,
      get: async () => {
        await this.store.ready();
        return this.store.getAll().map(enrollment => enrollment.slug);
      },
    });
    Object.defineProperty(context, "enrollmentsMap", {
      enumerable: true,
      get: async () => {
        await this.store.ready();
        return this.store.getAll().reduce((acc, enrollment) => {
          acc[enrollment.slug] = enrollment.branch.slug;
          return acc;
        }, {});
      },
    });
    return context;
  }

  /**
   * Runs on startup, including before first run.
   *
   * @param {object} extraContext extra targeting context provided by the
   * ambient environment.
   */
  async onStartup(extraContext = {}) {
    await this.store.init();
    this.extraContext = extraContext;

    const restoredExperiments = this.store.getAllActiveExperiments();
    const restoredRollouts = this.store.getAllActiveRollouts();

    for (const experiment of restoredExperiments) {
      lazy.NimbusTelemetry.setExperimentActive(experiment);
      if (this._restoreEnrollmentPrefs(experiment)) {
        this._updatePrefObservers(experiment);
      }
    }
    for (const rollout of restoredRollouts) {
      lazy.NimbusTelemetry.setExperimentActive(rollout);
      if (this._restoreEnrollmentPrefs(rollout)) {
        this._updatePrefObservers(rollout);
      }
    }

    this._prefFlips.init();

    if (!this.studiesEnabled) {
      this._handleStudiesOptOut();
    }

    lazy.NimbusFeatures.nimbusTelemetry.onUpdate(() => {
      // Providing default values ensure we disable metrics when unenrolling.
      const cfg = {
        metrics_enabled: {
          "nimbus_targeting_environment.targeting_context_value": false,
          "nimbus_events.enrollment_status": false,
        },
      };

      const overrides =
        lazy.NimbusFeatures.nimbusTelemetry.getVariable(
          "gleanMetricConfiguration"
        ) ?? {};

      for (const [key, value] of Object.entries(overrides)) {
        cfg[key] = { ...(cfg[key] ?? {}), ...value };
      }

      Services.fog.applyServerKnobsConfig(JSON.stringify(cfg));
    });
  }

  /**
   * Runs every time a Recipe is updated or seen for the first time.
   * @param {RecipeArgs} recipe
   * @param {string} source
   * @param {boolean} isTargetingMatch
   */
  async onRecipe(recipe, source, isTargetingMatch) {
    const { slug, isEnrollmentPaused, isFirefoxLabsOptIn } = recipe;

    if (!source) {
      throw new Error("When calling onRecipe, you must specify a source.");
    }

    if (isFirefoxLabsOptIn) {
      this.optInRecipes.push(recipe);
    }

    if (isTargetingMatch) {
      if (!this.sessions.has(source)) {
        this.sessions.set(source, new Set());
      }
      this.sessions.get(source).add(slug);

      if (this.store.has(slug)) {
        await this.updateEnrollment(recipe, source);
      } else if (!isFirefoxLabsOptIn) {
        // Firefox Labs opt-ins cannot be paused and we do not enroll in them
        // directly.
        if (isEnrollmentPaused) {
          lazy.log.debug(`Enrollment is paused for "${slug}"`);
        } else if (!(await this.isInBucketAllocation(recipe.bucketConfig))) {
          lazy.log.debug(
            "Client was not enrolled because of the bucket sampling"
          );
        } else {
          await this.enroll(recipe, source);
        }
      }
    }
  }

  _checkUnseenEnrollments(
    enrollments,
    sourceToCheck,
    recipeMismatches,
    invalidRecipes,
    invalidBranches,
    invalidFeatures,
    missingLocale,
    missingL10nIds
  ) {
    const { EnrollmentStatus, EnrollmentStatusReason, UnenrollReason } =
      lazy.NimbusTelemetry;

    for (const enrollment of enrollments) {
      const { slug, source, branch } = enrollment;
      if (sourceToCheck !== source) {
        continue;
      }
      const statusTelemetry = {
        slug,
        branch: branch.slug,
      };
      if (!this.sessions.get(source)?.has(slug)) {
        lazy.log.debug(`Stopping study for recipe ${slug}`);

        let reason;
        if (recipeMismatches.includes(slug)) {
          reason = UnenrollReason.TARGETING_MISMATCH;
          statusTelemetry.status = EnrollmentStatus.DISQUALIFIED;
          statusTelemetry.reason = EnrollmentStatusReason.NOT_TARGETED;
        } else if (invalidRecipes.includes(slug)) {
          reason = UnenrollReason.INVALID_RECIPE;
        } else if (invalidBranches.has(slug)) {
          reason = UnenrollReason.INVALID_BRANCH;
        } else if (invalidFeatures.has(slug)) {
          reason = UnenrollReason.INVALID_FEATURE;
        } else if (missingLocale.includes(slug)) {
          reason = UnenrollReason.L10N_MISSING_LOCALE;
        } else if (missingL10nIds.has(slug)) {
          reason = UnenrollReason.L10N_MISSING_ENTRY;
        } else {
          reason = UnenrollReason.RECIPE_NOT_SEEN;
          statusTelemetry.status = EnrollmentStatus.WAS_ENROLLED;
        }
        if (!statusTelemetry.status) {
          statusTelemetry.status = EnrollmentStatus.DISQUALIFIED;
          statusTelemetry.reason = EnrollmentStatusReason.ERROR;
          statusTelemetry.error_string = reason;
        }

        try {
          this.unenroll(slug, reason);
        } catch (err) {
          console.error(err);
        }
      } else {
        statusTelemetry.status = EnrollmentStatus.ENROLLED;
        statusTelemetry.reason = EnrollmentStatusReason.QUALIFIED;
      }

      lazy.NimbusTelemetry.recordEnrollmentStatus(statusTelemetry);
    }
  }

  /**
   * Removes stored enrollments that were not seen after syncing with Remote Settings
   * Runs when the all recipes been processed during an update, including at first run.
   * @param {string} sourceToCheck
   * @param {object} options Extra context used in telemetry reporting
   * @param {string[]} options.recipeMismatches
   *         The list of experiments that do not match targeting.
   * @param {string[]} options.invalidRecipes
   *         The list of recipes that do not match
   * @param {Map<string, string[]>} options.invalidBranches
   *         A mapping of experiment slugs to a list of branches that failed
   *         feature validation.
   * @param {Map<string, string[]>} options.invalidFeatures
   *        The mapping of experiment slugs to a list of invalid feature IDs.
   * @param {string[]} options.missingLocale
   *        The list of experiment slugs missing an entry in the localization
   *        table for the current locale.
   * @param {Map<string, string[]>} options.missingL10nIds
   *        The mapping of experiment slugs to the IDs of localization entries
   *        missing from the current locale.
   * @param {string | null} options.locale
   *        The current locale.
   * @param {boolean} options.validationEnabled
   *        Whether or not schema validation was enabled.
   */
  onFinalize(
    sourceToCheck,
    {
      recipeMismatches = [],
      invalidRecipes = [],
      invalidBranches = new Map(),
      invalidFeatures = new Map(),
      missingLocale = [],
      missingL10nIds = new Map(),
      locale = null,
      validationEnabled = true,
    } = {}
  ) {
    if (!sourceToCheck) {
      throw new Error("When calling onFinalize, you must specify a source.");
    }
    const activeExperiments = this.store.getAllActiveExperiments();
    const activeRollouts = this.store.getAllActiveRollouts();
    this._checkUnseenEnrollments(
      activeExperiments,
      sourceToCheck,
      recipeMismatches,
      invalidRecipes,
      invalidBranches,
      invalidFeatures,
      missingLocale,
      missingL10nIds
    );
    this._checkUnseenEnrollments(
      activeRollouts,
      sourceToCheck,
      recipeMismatches,
      invalidRecipes,
      invalidBranches,
      invalidFeatures,
      missingLocale,
      missingL10nIds
    );

    // If schema validation is disabled, then we will never send these
    // validation failed telemetry events
    if (validationEnabled) {
      for (const slug of invalidRecipes) {
        lazy.NimbusTelemetry.recordValidationFailure(
          slug,
          lazy.NimbusTelemetry.ValidationFailureReason.INVALID_RECIPE
        );
      }
      for (const [slug, branches] of invalidBranches.entries()) {
        for (const branch of branches) {
          lazy.NimbusTelemetry.recordValidationFailure(
            slug,
            lazy.NimbusTelemetry.ValidationFailureReason.INVALID_BRANCH,
            {
              branch,
            }
          );
        }
      }
      for (const [slug, featureIds] of invalidFeatures.entries()) {
        for (const featureId of featureIds) {
          lazy.NimbusTelemetry.recordValidationFailure(
            slug,
            lazy.NimbusTelemetry.ValidationFailureReason.INVALID_FEATURE,
            {
              feature: featureId,
            }
          );
        }
      }
    }

    if (locale) {
      for (const slug of missingLocale.values()) {
        lazy.NimbusTelemetry.recordValidationFailure(
          slug,
          lazy.NimbusTelemetry.ValidationFailureReason.L10N_MISSING_LOCALE,
          { locale }
        );
      }

      for (const [slug, ids] of missingL10nIds.entries()) {
        lazy.NimbusTelemetry.recordValidationFailure(
          slug,
          lazy.NimbusTelemetry.ValidationFailureReason.L10N_MISSING_ENTRY,
          {
            l10nIds: ids.join(","),
            locale,
          }
        );
      }
    }

    this.sessions.delete(sourceToCheck);
    this._originalDefaultValues = null;
  }

  /**
   * Determine userId based on bucketConfig.randomizationUnit;
   * either "normandy_id" or "group_id".
   *
   * @param {object} bucketConfig
   *
   */
  async getUserId(bucketConfig) {
    let id;
    if (bucketConfig.randomizationUnit === "normandy_id") {
      id = lazy.ClientEnvironment.userId;
    } else if (bucketConfig.randomizationUnit === "group_id") {
      id = await lazy.ClientID.getProfileGroupID();
    } else {
      // Others not currently supported.
      lazy.log.debug(
        `Invalid randomizationUnit: ${bucketConfig.randomizationUnit}`
      );
    }
    return id;
  }

  /**
   * Get all of the opt-in recipes that match targeting and bucketing.
   *
   * @returns opt in recipes
   */
  async getAllOptInRecipes() {
    const enrollmentsCtx = new lazy.EnrollmentsContext(this, null, {
      validationEnabled: false,
    });

    // RemoteSettingsExperimentLoader could be in a middle of updating recipes
    // so let's wait for the update to finish and this promise to resolve.
    await lazy.ExperimentAPI._rsLoader.finishedUpdating();

    // RemoteSettingsExperimentLoader should have finished updating at least
    // once. Prevent concurrent updates while we filter through the list of
    // available opt-in recipes.
    return lazy.ExperimentAPI._rsLoader.withUpdateLock(
      async () => {
        const filtered = [];

        for (const recipe of this.optInRecipes) {
          if (
            (await enrollmentsCtx.checkTargeting(recipe)) &&
            (await this.isInBucketAllocation(recipe.bucketConfig))
          ) {
            filtered.push(recipe);
          }
        }

        return filtered;
      },
      { mode: "shared" }
    );
  }

  /**
   * Get a single opt in recipe given its slug.
   *
   * @returns a single opt in recipe or undefined if not found.
   */
  async getSingleOptInRecipe(slug) {
    if (!slug) {
      throw new Error("Slug required for .getSingleOptInRecipe");
    }

    // RemoteSettingsExperimentLoader could be in a middle of updating recipes
    // so let's wait for the update to finish and this promise to resolve.
    await lazy.ExperimentAPI._rsLoader.finishedUpdating();

    // We don't need to hold the RSEL lock here because we are not doing any async work.
    return this.optInRecipes.find(recipe => recipe.slug === slug);
  }

  /**
   * Determine if this client falls into the bucketing specified in bucketConfig
   *
   * @param {object} bucketConfig
   * @param {string} bucketConfig.randomizationUnit
   *                 The randomization unit to use for bucketing. This must be
   *                 either "normandy_id" or "group_id".
   * @param {number} bucketConfig.start
   *                 The start of the bucketing range (inclusive).
   * @param {number} bucketConfig.count
   *                 The number of buckets in the range.
   * @param {number} bucketConfig.total
   *                 The total number of buckets.
   * @param {string} bucketConfig.namespace
   *                 A namespace used to seed the RNG used in the sampling
   *                 algorithm. Given an otherwise identical bucketConfig with
   *                 different namespaces, the client will fall into different a
   *                 different bucket.
   * @returns {Promise<boolean>}
   *          Whether or not the client falls into the bucketing range.
   */
  async isInBucketAllocation(bucketConfig) {
    if (!bucketConfig) {
      lazy.log.debug("Cannot enroll if recipe bucketConfig is not set.");
      return false;
    }

    const id = await this.getUserId(bucketConfig);
    if (!id) {
      return false;
    }

    return lazy.Sampling.bucketSample(
      [id, bucketConfig.namespace],
      bucketConfig.start,
      bucketConfig.count,
      bucketConfig.total
    );
  }

  /**
   * Start a new experiment by enrolling the users
   *
   * @param {object} recipe
   *                 The recipe to enroll in.
   * @param {string} source
   *                 The source of the experiment (e.g., "rs-loader" for recipes
   *                 from Remote Settings).
   * @param {object} options
   * @param {boolean} options.reenroll
   *                  Allow re-enrollment. Only supported for rollouts.
   * @param {string} options.branchSlug
   *                 If enrolling in a Firefox Labs opt-in experiment, this
   *                 option is required and will dictate which branch to enroll
   *                 in.
   *
   * @returns {Promise<Enrollment>}
   *          The experiment object stored in the data store.
   *
   * @throws {Error} If a recipe already exists in the store with the same slug
   *                 as `recipe` and re-enrollment is prevented.
   */
  async enroll(recipe, source, { reenroll = false, branchSlug } = {}) {
    let { slug, branches, bucketConfig, isFirefoxLabsOptIn } = recipe;

    const enrollment = this.store.get(slug);

    if (
      enrollment &&
      (enrollment.active ||
        (!isFirefoxLabsOptIn && (!enrollment.isRollout || !reenroll)))
    ) {
      lazy.NimbusTelemetry.recordEnrollmentFailure(
        slug,
        lazy.NimbusTelemetry.EnrollmentFailureReason.NAME_CONFLICT
      );
      throw new Error(`An experiment with the slug "${slug}" already exists.`);
    }

    let storeLookupByFeature = recipe.isRollout
      ? this.store.getRolloutForFeature.bind(this.store)
      : this.store.hasExperimentForFeature.bind(this.store);
    const userId = await this.getUserId(bucketConfig);

    let branch;

    if (isFirefoxLabsOptIn) {
      if (typeof branchSlug === "undefined") {
        throw new TypeError(
          `Branch slug not provided for Firefox Labs opt in recipe: "${slug}"`
        );
      } else {
        branch = branches.find(branch => branch.slug === branchSlug);

        if (!branch) {
          throw new Error(
            `Invalid branch slug provided for Firefox Labs opt in recipe: "${slug}"`
          );
        }
      }
    } else if (typeof branchSlug !== "undefined") {
      throw new TypeError(
        "branchSlug only supported for recipes with isFirefoxLabsOptIn = true"
      );
    } else {
      // recipe is not an opt in recipe hence use a ratio sampled branch
      branch = await this.chooseBranch(slug, branches, userId);
    }

    const features = featuresCompat(branch);
    for (let feature of features) {
      if (storeLookupByFeature(feature?.featureId)) {
        lazy.log.debug(
          `Skipping enrollment for "${slug}" because there is an existing ${
            recipe.isRollout ? "rollout" : "experiment"
          } for this feature.`
        );
        lazy.NimbusTelemetry.recordEnrollmentFailure(
          slug,
          lazy.NimbusTelemetry.EnrollmentFailureReason.FEATURE_CONFLICT
        );

        return null;
      }
    }

    return this._enroll(recipe, branch, source);
  }

  _enroll(
    {
      slug,
      experimentType = TELEMETRY_DEFAULT_EXPERIMENT_TYPE,
      userFacingName,
      userFacingDescription,
      featureIds,
      isRollout,
      localizations,
      isFirefoxLabsOptIn,
      firefoxLabsTitle,
      firefoxLabsDescription,
      firefoxLabsDescriptionLinks = null,
      firefoxLabsGroup,
      requiresRestart = false,
    },
    branch,
    source,
    options = {}
  ) {
    const { prefs, prefsToSet } = this._getPrefsForBranch(branch, isRollout);
    const prefNames = new Set(prefs.map(entry => entry.name));

    // Unenroll in any conflicting prefFlips enrollments. Even though the
    // rollout does not have an effect, if it also *would* control any of the
    // same prefs, it would cause a conflict when it became active.
    const prefFlipEnrollments = [
      this.store.getRolloutForFeature(PrefFlipsFeature.FEATURE_ID),
      this.store.getExperimentForFeature(PrefFlipsFeature.FEATURE_ID),
    ].filter(enrollment => enrollment);

    for (const enrollment of prefFlipEnrollments) {
      const featureValue = getFeatureFromBranch(
        enrollment.branch,
        PrefFlipsFeature.FEATURE_ID
      ).value;

      for (const prefName of Object.keys(featureValue.prefs)) {
        if (prefNames.has(prefName)) {
          this._unenroll(enrollment, {
            reason: lazy.NimbusTelemetry.UnenrollReason.PREF_FLIPS_CONFLICT,
            conflictingSlug: slug,
          });
          break;
        }
      }
    }

    /** @type {Enrollment} */
    const enrollment = {
      slug,
      branch,
      active: true,
      experimentType,
      source,
      userFacingName,
      userFacingDescription,
      lastSeen: new Date().toJSON(),
      featureIds,
      prefs,
    };

    if (localizations) {
      enrollment.localizations = localizations;
    }

    if (typeof isFirefoxLabsOptIn !== "undefined") {
      Object.assign(enrollment, {
        isFirefoxLabsOptIn,
        firefoxLabsTitle,
        firefoxLabsDescription,
        firefoxLabsDescriptionLinks,
        firefoxLabsGroup,
        requiresRestart,
      });
    }

    if (typeof isRollout !== "undefined") {
      enrollment.isRollout = isRollout;
    }

    // Tag this as a forced enrollment. This prevents all unenrolling unless
    // manually triggered from about:studies
    if (options.force) {
      enrollment.force = true;
    }

    if (isRollout) {
      enrollment.experimentType = "rollout";
      this.store.addEnrollment(enrollment);
    } else {
      this.store.addEnrollment(enrollment);
    }

    lazy.NimbusTelemetry.recordEnrollment(enrollment);

    this._setEnrollmentPrefs(prefsToSet);
    this._updatePrefObservers(enrollment);

    lazy.log.debug(
      `New ${isRollout ? "rollout" : "experiment"} started: ${slug}, ${
        branch.slug
      }`
    );

    return enrollment;
  }

  forceEnroll(recipe, branch, source = "force-enrollment") {
    /**
     * If we happen to be enrolled in an experiment for the same feature
     * we need to unenroll from that experiment.
     * If the experiment has the same slug after unenrollment adding it to the
     * store will overwrite the initial experiment.
     */
    const features = featuresCompat(branch);
    for (let feature of features) {
      const isRollout = recipe.isRollout ?? false;
      let enrollment = isRollout
        ? this.store.getRolloutForFeature(feature?.featureId)
        : this.store.getExperimentForFeature(feature?.featureId);
      if (enrollment) {
        lazy.log.debug(
          `Existing ${
            isRollout ? "rollout" : "experiment"
          } found for the same feature ${feature.featureId}, unenrolling.`
        );

        this.unenroll(enrollment.slug, source);
      }
    }

    recipe.userFacingName = `${recipe.userFacingName} - Forced enrollment`;

    const slug = `optin-${recipe.slug}`;
    const enrollment = this._enroll(
      {
        ...recipe,
        slug,
      },
      branch,
      source,
      { force: true }
    );

    Services.obs.notifyObservers(null, "nimbus:enrollments-updated", slug);

    return enrollment;
  }

  /**
   * Update an enrollment that was already set
   *
   * @param {RecipeArgs} recipe
   * @returns {boolean} whether the enrollment is still active
   */
  async updateEnrollment(recipe, source) {
    /** @type Enrollment */
    const enrollment = this.store.get(recipe.slug);

    // Don't update experiments that were already unenrolled.
    if (enrollment.active === false && !recipe.isRollout) {
      lazy.log.debug(`Enrollment ${recipe.slug} has expired, aborting.`);
      return false;
    }

    if (recipe.isRollout) {
      if (!(await this.isInBucketAllocation(recipe.bucketConfig))) {
        lazy.log.debug(
          `No longer meet bucketing for "${recipe.slug}"; unenrolling...`
        );
        this.unenroll(
          recipe.slug,
          lazy.NimbusTelemetry.UnenrollReason.BUCKETING
        );
        return false;
      } else if (
        !enrollment.active &&
        enrollment.unenrollReason !==
          lazy.NimbusTelemetry.UnenrollReason.INDIVIDUAL_OPT_OUT &&
        !enrollment.isFirefoxLabsOptIn
      ) {
        lazy.log.debug(`Re-enrolling in rollout "${recipe.slug}`);
        return !!(await this.enroll(recipe, source, { reenroll: true }));
      }
    }

    // Stay in the same branch, don't re-sample every time.
    const branch = recipe.branches.find(
      branch => branch.slug === enrollment.branch.slug
    );

    if (!branch) {
      // Our branch has been removed. Unenroll.
      this.unenroll(
        recipe.slug,
        lazy.NimbusTelemetry.UnenrollReason.BRANCH_REMOVED
      );
      return false;
    }

    return true;
  }

  /**
   * Stop an enrollment that is currently active
   *
   * @param {string} slug
   *        The slug of the enrollment to stop.
   * @param {string} reason
   *        An optional reason for the unenrollment. If not provided, "unknown"
   *        will be used.
   *
   *        This will be reported in telemetry.
   */
  unenroll(slug, reason) {
    const enrollment = this.store.get(slug);
    if (!enrollment) {
      lazy.NimbusTelemetry.recordUnenrollmentFailure(
        slug,
        lazy.NimbusTelemetry.UnenrollmentFailureReason.DOES_NOT_EXIST
      );
      throw new Error(`Could not find an experiment with the slug "${slug}"`);
    }

    this._unenroll(enrollment, {
      reason: reason ?? lazy.NimbusTelemetry.UnenrollReason.UNKNOWN,
    });
  }

  /**
   * Stop an enrollment that is currently active.
   *
   * @param {Enrollment} enrollment
   *        The enrollment to end.
   *
   * @param {object} options
   * @param {string} options.reason
   *        An optional reason for the unenrollment.
   *
   *        This will be reported in telemetry.
   *
   * @param {object?} options.changedPref
   *        If the unenrollment was due to pref change, this will contain the
   *        information about the pref that changed.
   *
   * @param {string} options.changedPref.name
   *        The name of the pref that caused the unenrollment.
   *
   * @param {string} options.changedPref.branch
   *        The branch that was changed ("user" or "default").
   */
  _unenroll(
    enrollment,
    {
      reason = "unknown",
      changedPref = undefined,
      duringRestore = false,
      conflictingSlug = undefined,
      prefName = undefined,
      prefType = undefined,
    } = {}
  ) {
    const { slug } = enrollment;

    if (!enrollment.active) {
      lazy.NimbusTelemetry.recordUnenrollmentFailure(
        slug,
        lazy.NimbusTelemetry.UnenrollmentFailureReason.ALREADY_UNENROLLED
      );
      throw new Error(
        `Cannot stop experiment "${slug}" because it is already expired`
      );
    }

    this.store.updateExperiment(slug, {
      active: false,
      unenrollReason: reason,
    });

    lazy.NimbusTelemetry.recordUnenrollment(
      slug,
      reason,
      enrollment.branch.slug,
      {
        changedPref,
        conflictingSlug,
        prefType,
        prefName,
      }
    );

    this._unsetEnrollmentPrefs(enrollment, { changedPref, duringRestore });

    lazy.log.debug(`Recipe unenrolled: ${slug}`);
  }

  observe() {
    if (!this.studiesEnabled) {
      this._handleStudiesOptOut();
    }

    Services.obs.notifyObservers(null, STUDIES_ENABLED_CHANGED);
  }

  /**
   * Unenroll from all active studies if user opts out.
   */
  _handleStudiesOptOut() {
    for (const { slug } of this.store.getAllActiveExperiments()) {
      this.unenroll(slug, lazy.NimbusTelemetry.UnenrollReason.STUDIES_OPT_OUT);
    }
    for (const { slug } of this.store.getAllActiveRollouts()) {
      this.unenroll(slug, lazy.NimbusTelemetry.UnenrollReason.STUDIES_OPT_OUT);
    }

    this.optInRecipes = [];
  }

  /**
   * Generate Normandy UserId respective to a branch
   * for a given experiment.
   *
   * @param {string} slug
   * @param {Array<{slug: string; ratio: number}>} branches
   * @param {string} namespace
   * @param {number} start
   * @param {number} count
   * @param {number} total
   * @returns {Promise<{[branchName: string]: string}>} An object where
   * the keys are branch names and the values are user IDs that will enroll
   * a user for that particular branch. Also includes a `notInExperiment` value
   * that will not enroll the user in the experiment if not 100% enrollment.
   */
  async generateTestIds(recipe) {
    // Older recipe structure had bucket config values at the top level while
    // newer recipes group them into a bucketConfig object
    const { slug, branches, namespace, start, count, total } = {
      ...recipe,
      ...recipe.bucketConfig,
    };
    const branchValues = {};
    const includeNot = count < total;

    if (!slug || !namespace) {
      throw new Error(`slug, namespace not in expected format`);
    }

    if (!(start < total && count <= total)) {
      throw new Error("Must include start, count, and total as integers");
    }

    if (
      !Array.isArray(branches) ||
      branches.filter(branch => branch.slug && branch.ratio).length !==
        branches.length
    ) {
      throw new Error("branches parameter not in expected format");
    }

    while (Object.keys(branchValues).length < branches.length + includeNot) {
      const id = lazy.NormandyUtils.generateUuid();
      const enrolls = await lazy.Sampling.bucketSample(
        [id, namespace],
        start,
        count,
        total
      );
      // Does this id enroll the user in the experiment
      if (enrolls) {
        // Choose a random branch
        const { slug: pickedBranch } = await this.chooseBranch(
          slug,
          branches,
          id
        );

        if (!Object.keys(branchValues).includes(pickedBranch)) {
          branchValues[pickedBranch] = id;
          lazy.log.debug(`Found a value for "${pickedBranch}"`);
        }
      } else if (!branchValues.notInExperiment) {
        branchValues.notInExperiment = id;
      }
    }
    return branchValues;
  }

  /**
   * Choose a branch randomly.
   *
   * @param {string} slug
   * @param {Branch[]} branches
   * @param {string} userId
   * @returns {Promise<Branch>}
   * @memberof _ExperimentManager
   */
  async chooseBranch(slug, branches, userId = lazy.ClientEnvironment.userId) {
    const ratios = branches.map(({ ratio = 1 }) => ratio);

    // It's important that the input be:
    // - Unique per-user (no one is bucketed alike)
    // - Unique per-experiment (bucketing differs across multiple experiments)
    // - Differs from the input used for sampling the recipe (otherwise only
    //   branches that contain the same buckets as the recipe sampling will
    //   receive users)
    const input = `${this.id}-${userId}-${slug}-branch`;

    const index = await lazy.Sampling.ratioSample(input, ratios);
    return branches[index];
  }

  /**
   * Generate the list of prefs a recipe will set.
   *
   * @params {object} branch The recipe branch that will be enrolled.
   * @params {boolean} isRollout Whether or not this recipe is a rollout.
   *
   * @returns {object} An object with the following keys:
   *
   *                   `prefs`:
   *                        The full list of prefs that this recipe would set,
   *                        if there are no conflicts. This will include prefs
   *                        that, for example, will not be set because this
   *                        enrollment is a rollout and there is an active
   *                        experiment that set the same pref.
   *
   *                   `prefsToSet`:
   *                        Prefs that should be set once enrollment is
   *                        complete.
   */
  _getPrefsForBranch(branch, isRollout = false) {
    const prefs = [];
    const prefsToSet = [];

    const getConflictingEnrollment = this._makeEnrollmentCache(isRollout);

    for (const { featureId, value: featureValue } of featuresCompat(branch)) {
      const feature = lazy.NimbusFeatures[featureId];

      if (!feature) {
        continue;
      }

      // It is possible to enroll in both an experiment and a rollout, so we
      // need to check if we have another enrollment for the same feature.
      const conflictingEnrollment = getConflictingEnrollment(featureId);

      for (let [variable, value] of Object.entries(featureValue)) {
        const setPref = feature.getSetPref(variable);

        if (setPref) {
          const { pref: prefName, branch: prefBranch } = setPref;

          let originalValue;
          const conflictingPref = conflictingEnrollment?.prefs?.find(
            p => p.name === prefName
          );

          if (conflictingPref) {
            // If there is another enrollment that has already set the pref we
            // care about, we use its stored originalValue.
            originalValue = conflictingPref.originalValue;
          } else if (
            prefBranch === "user" &&
            !Services.prefs.prefHasUserValue(prefName)
          ) {
            // If there is a default value set, then attempting to read the user
            // branch would result in returning the default branch value.
            originalValue = null;
          } else {
            // If there is an active prefFlips experiment for this pref on this
            // branch, we must use its originalValue.
            const prefFlip = this._prefFlips._prefs.get(prefName);
            if (prefFlip?.branch === prefBranch) {
              originalValue = prefFlip.originalValue;
            } else {
              originalValue = lazy.PrefUtils.getPref(prefName, {
                branch: prefBranch,
              });
            }
          }

          prefs.push({
            name: prefName,
            branch: prefBranch,
            featureId,
            variable,
            originalValue,
          });

          // An experiment takes precedence if there is already a pref set.
          if (!isRollout || !conflictingPref) {
            if (
              lazy.NimbusFeatures[featureId].manifest.variables[variable]
                .type === "json"
            ) {
              value = JSON.stringify(value);
            }

            prefsToSet.push({
              name: prefName,
              value,
              prefBranch,
            });
          }
        }
      }
    }

    return { prefs, prefsToSet };
  }

  /**
   * Set a list of prefs from enrolling in an experiment or rollout.
   *
   * The ExperimentManager's pref observers will be disabled while setting each
   * pref so as not to accidentally unenroll an existing rollout that an
   * experiment would override.
   *
   * @param {object[]} prefsToSet
   *                   A list of objects containing the prefs to set.
   *
   *                   Each object has the following properties:
   *
   *                   * `name`: The name of the pref.
   *                   * `value`: The value of the pref.
   *                   * `prefBranch`: The branch to set the pref on (either "user" or "default").
   */
  _setEnrollmentPrefs(prefsToSet) {
    for (const { name, value, prefBranch } of prefsToSet) {
      const entry = this._prefs.get(name);

      // If another enrollment exists that has set this pref, temporarily
      // disable the pref observer so as not to cause unenrollment.
      if (entry) {
        entry.enrollmentChanging = true;
      }

      lazy.PrefUtils.setPref(name, value, { branch: prefBranch });

      if (entry) {
        entry.enrollmentChanging = false;
      }
    }
  }

  /**
   * Unset prefs set during this enrollment.
   *
   * If this enrollment is an experiment and there is an existing rollout that
   * would set a pref that was covered by this enrollment, the pref will be
   * updated to that rollout's value.
   *
   * Otherwise, it will be set to the original value from before the enrollment
   * began.
   *
   * @param {Enrollment} enrollment
   *        The enrollment that has ended.
   *
   * @param {object} options
   *
   * @param {object?} options.changedPref
   *        If provided, a changed pref that caused the unenrollment that
   *        triggered unsetting these prefs. This is provided as to not
   *        overwrite a changed pref with an original value.
   *
   * @param {string} options.changedPref.name
   *        The name of the changed pref.
   *
   * @param {string} options.changedPref.branch
   *        The branch that was changed ("user" or "default").
   *
   * @param {boolean} options.duringRestore
   *        The unenrollment was caused during restore.
   */
  _unsetEnrollmentPrefs(enrollment, { changedPref, duringRestore } = {}) {
    if (!enrollment.prefs?.length) {
      return;
    }

    const getConflictingEnrollment = this._makeEnrollmentCache(
      enrollment.isRollout
    );

    for (const pref of enrollment.prefs) {
      this._removePrefObserver(pref.name, enrollment.slug);

      if (
        changedPref?.name == pref.name &&
        changedPref.branch === pref.branch
      ) {
        // Resetting the original value would overwite the pref the user just
        // set. Skip it.
        continue;
      }

      let newValue = pref.originalValue;

      // If we are unenrolling from an experiment during a restore, we must
      // ignore any potential conflicting rollout in the store, because its
      // hasn't gone through `_restoreEnrollmentPrefs`, which might also cause
      // it to unenroll.
      //
      // Both enrollments will have the same `originalValue` stored, so it will
      // always be restored.
      if (!duringRestore || enrollment.isRollout) {
        const conflictingEnrollment = getConflictingEnrollment(pref.featureId);
        const conflictingPref = conflictingEnrollment?.prefs?.find(
          p => p.name === pref.name
        );

        if (conflictingPref) {
          if (enrollment.isRollout) {
            // If we are unenrolling from a rollout, we have an experiment that
            // has set the pref. Since experiments take priority, we do not unset
            // it.
            continue;
          } else {
            // If we are an unenrolling from an experiment, we have a rollout that would
            // set the same pref, so we update the pref to that value instead of
            // the original value.
            newValue = getFeatureFromBranch(
              conflictingEnrollment.branch,
              pref.featureId
            ).value[pref.variable];
          }
        }
      }

      // If another enrollment exists that has set this pref, temporarily
      // disable the pref observer so as not to cause unenrollment when we
      // update the pref to its value.
      const entry = this._prefs.get(pref.name);
      if (entry) {
        entry.enrollmentChanging = true;
      }

      lazy.PrefUtils.setPref(pref.name, newValue, {
        branch: pref.branch,
      });

      if (entry) {
        entry.enrollmentChanging = false;
      }
    }
  }

  /**
   * Restore the prefs set by an enrollment.
   *
   * @param {object} enrollment The enrollment.
   * @param {object} enrollment.branch The branch that was enrolled.
   * @param {object[]} enrollment.prefs The prefs that are set by the enrollment.
   * @param {object[]} enrollment.isRollout The prefs that are set by the enrollment.
   *
   * @returns {boolean} Whether the restore was successful. If false, the
   *                    enrollment has ended.
   */
  _restoreEnrollmentPrefs(enrollment) {
    const { branch, prefs = [], isRollout } = enrollment;

    if (!prefs?.length) {
      return false;
    }

    const featuresById = Object.assign(
      ...featuresCompat(branch).map(f => ({ [f.featureId]: f }))
    );

    for (const { name, featureId, variable } of prefs) {
      // If the feature no longer exists, unenroll.
      if (!Object.hasOwn(lazy.NimbusFeatures, featureId)) {
        this._unenroll(enrollment, {
          reason: lazy.NimbusTelemetry.UnenrollReason.INVALID_FEATURE,
          duringRestore: true,
        });
        return false;
      }

      const variables = lazy.NimbusFeatures[featureId].manifest.variables;

      // If the feature is missing a variable that set a pref, unenroll.
      if (!Object.hasOwn(variables, variable)) {
        this._unenroll(enrollment, {
          reason: lazy.NimbusTelemetry.UnenrollReason.PREF_VARIABLE_MISSING,
          duringRestore: true,
        });
        return false;
      }

      const variableDef = variables[variable];

      // If the variable is no longer a pref-setting variable, unenroll.
      if (!Object.hasOwn(variableDef, "setPref")) {
        this._unenroll(enrollment, {
          reason: lazy.NimbusTelemetry.UnenrollReason.PREF_VARIABLE_NO_LONGER,
          duringRestore: true,
        });
        return false;
      }

      // If the variable is setting a different preference, unenroll.
      const prefName =
        typeof variableDef.setPref === "object"
          ? variableDef.setPref.pref
          : variableDef.setPref;

      if (prefName !== name) {
        this._unenroll(enrollment, {
          reason: lazy.NimbusTelemetry.UnenrollReason.PREF_VARIABLE_CHANGED,
          duringRestore: true,
        });
        return false;
      }
    }

    for (const { name, branch: prefBranch, featureId, variable } of prefs) {
      // User prefs are already persisted.
      if (prefBranch === "user") {
        continue;
      }

      // If we are a rollout, we need to check for an existing experiment that
      // has set the same pref. If so, we do not need to set the pref because
      // experiments take priority.
      if (isRollout) {
        const conflictingEnrollment =
          this.store.getExperimentForFeature(featureId);
        const conflictingPref = conflictingEnrollment?.prefs?.find(
          p => p.name === name
        );

        if (conflictingPref) {
          continue;
        }
      }

      let value = featuresById[featureId].value[variable];
      if (
        lazy.NimbusFeatures[featureId].manifest.variables[variable].type ===
        "json"
      ) {
        value = JSON.stringify(value);
      }

      if (prefBranch !== "user") {
        lazy.PrefUtils.setPref(name, value, { branch: prefBranch });
      }
    }

    return true;
  }

  /**
   * Make a cache to look up enrollments of the oppposite kind by feature ID.
   *
   * @param {boolean} isRollout Whether or not the current enrollment is a
   *                            rollout. If true, the cache will return
   *                            experiments. If false, the cache will return
   *                            rollouts.
   *
   * @returns {function} The cache, as a callable function.
   */
  _makeEnrollmentCache(isRollout) {
    const getOtherEnrollment = (
      isRollout
        ? this.store.getExperimentForFeature
        : this.store.getRolloutForFeature
    ).bind(this.store);

    const conflictingEnrollments = {};
    return featureId => {
      if (!Object.hasOwn(conflictingEnrollments, featureId)) {
        conflictingEnrollments[featureId] = getOtherEnrollment(featureId);
      }

      return conflictingEnrollments[featureId];
    };
  }

  /**
   * Update the set of observers with prefs set by the given enrollment.
   *
   * @param {Enrollment} enrollment
   *        The enrollment that is setting prefs.
   */
  _updatePrefObservers({ slug, prefs }) {
    if (!prefs?.length) {
      return;
    }

    for (const pref of prefs) {
      const { name } = pref;

      if (!this._prefs.has(name)) {
        const observer = (aSubject, aTopic, aData) => {
          // This observer will be called for changes to `name` as well as any
          // other pref that begins with `name.`, so we have to filter to
          // exactly the pref we care about.
          if (aData === name) {
            this._onExperimentPrefChanged(pref);
          }
        };
        const entry = {
          slugs: new Set([slug]),
          enrollmentChanging: false,
          observer,
        };

        Services.prefs.addObserver(name, observer);

        this._prefs.set(name, entry);
      } else {
        this._prefs.get(name).slugs.add(slug);
      }

      if (!this._prefsBySlug.has(slug)) {
        this._prefsBySlug.set(slug, new Set([name]));
      } else {
        this._prefsBySlug.get(slug).add(name);
      }
    }
  }

  /**
   * Remove an entry for the pref observer for the given pref and slug.
   *
   * If there are no more enrollments listening to a pref, the observer will be removed.
   *
   * This is called when an enrollment is ending.
   *
   * @param {string} name The name of the pref.
   * @param {string} slug The slug of the enrollment that is being unenrolled.
   */
  _removePrefObserver(name, slug) {
    // Update the pref observer that the current enrollment is no longer
    // involved in the pref.
    //
    // If no enrollments have a variable setting the pref, then we can remove
    // the observers.
    const entry = this._prefs.get(name);

    // If this is happening due to a pref change, the observers will already be removed.
    if (entry) {
      entry.slugs.delete(slug);
      if (entry.slugs.size == 0) {
        Services.prefs.removeObserver(name, entry.observer);
        this._prefs.delete(name);
      }
    }

    const bySlug = this._prefsBySlug.get(slug);
    if (bySlug) {
      bySlug.delete(name);
      if (bySlug.size == 0) {
        this._prefsBySlug.delete(slug);
      }
    }
  }

  /**
   * Handle a change to a pref set by enrollments by ending those enrollments.
   *
   * @param {object} pref
   *        Information about the pref that was changed.
   *
   * @param {string} pref.name
   *        The name of the pref that was changed.
   *
   * @param {string} pref.branch
   *        The branch enrollments set the pref on.
   *
   * @param {string} pref.featureId
   *        The feature ID of the feature containing the variable that set the
   *        pref.
   *
   * @param {string} pref.variable
   *        The variable in the given feature whose value determined the pref's
   *        value.
   */
  _onExperimentPrefChanged(pref) {
    const entry = this._prefs.get(pref.name);
    // If this was triggered while we are enrolling or unenrolling from an
    // experiment, then we don't want to unenroll from the rollout because the
    // experiment's value is taking precendence.
    //
    // Otherwise, all enrollments that set the variable corresponding to this
    // pref must be unenrolled.
    if (entry.enrollmentChanging) {
      return;
    }

    // Copy the `Set` into an `Array` because we modify the set later in
    // `_removePrefObserver` and we need to iterate over it multiple times.
    const slugs = Array.from(entry.slugs);

    // Remove all pref observers set by enrollments. We are potentially about
    // to set these prefs during unenrollment, so we don't want to trigger
    // them and cause nested unenrollments.
    for (const slug of slugs) {
      const toRemove = Array.from(this._prefsBySlug.get(slug) ?? []);
      for (const name of toRemove) {
        this._removePrefObserver(name, slug);
      }
    }

    // Unenroll from the rollout first to save calls to setPref.
    const enrollments = Array.from(slugs).map(slug => this.store.get(slug));

    // There is a maximum of two enrollments (one experiment and one rollout).
    if (enrollments.length == 2) {
      // Order enrollments so that we unenroll from the rollout first.
      if (!enrollments[0].isRollout) {
        enrollments.reverse();
      }
    }

    const feature = getFeatureFromBranch(
      enrollments.at(-1).branch,
      pref.featureId
    );

    const changedPref = {
      name: pref.name,
      branch: PrefFlipsFeature.determinePrefChangeBranch(
        pref.name,
        pref.branch,
        feature.value[pref.variable]
      ),
    };

    for (const enrollment of enrollments) {
      this._unenroll(enrollment, {
        reason: lazy.NimbusTelemetry.UnenrollReason.CHANGED_PREF,
        changedPref,
      });
    }
  }
}

export const ExperimentManager = new _ExperimentManager();
