struct StatusMeasurementCounters {
    size_t extents = 0, margins = 0, localeQueries = 0, formatLoads = 0;
};
extern StatusMeasurementCounters statusMeasurementCounters;
extern bool statusPresentationBaseline;
