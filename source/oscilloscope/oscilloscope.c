//
// Created on 3/18/25.
//

#include "oscilloscope.h"
#include <ogc/lwp_watchdog.h>
#include "../print.h"
#include "../draw.h"
#include "../polling.h"
#include "../stickmap_coordinates.h"
#include "../waveform.h"

#define STICK_MOVEMENT_THRESHOLD 5
#define STICK_TIME_THRESHOLD_MS 100
#define MEASURE_COOLDOWN_FRAMES 5

#define SCREEN_TIMEPLOT_START 70

static const uint32_t COLOR_RED_C = 0x846084d7;
static const uint32_t COLOR_BLUE_C = 0x6dd26d72;

static enum MENU_STATE state = OSC_SETUP;
static enum OSCILLOSCOPE_STATE oState = PRE_INPUT;

static WaveformData data = { {{ 0 }}, 0, 500, false, false };
static enum OSCILLOSCOPE_TEST currentTest = SNAPBACK;
static int waveformScaleFactor = 1;
static int dataScrollOffset = 0;
static char strBuffer[100];

static u8 stickCooldown = 0;
static bool pressLocked = false;
static bool stickMove = false;
static bool display = false;

static u8 ellipseCounter = 0;
static u64 pressedTimer = 0;
static u64 prevSampleCallbackTick = 0;
static u64 sampleCallbackTick = 0;
static u64 timeStickInOrigin = 0;

static u32 *pressed;
static u32 *held;
static bool buttonLock = false;

static sampling_callback cb;
static void oscilloscopeCallback() {
	// time from last call of this function calculation
	prevSampleCallbackTick = sampleCallbackTick;
	sampleCallbackTick = gettime();
	if (prevSampleCallbackTick == 0) {
		prevSampleCallbackTick = sampleCallbackTick;
	}

	static s8 x, y;
	PAD_ScanPads();

	// keep buttons in a "pressed" state long enough for code to see it
	// TODO: I don't like this implementation
	if (!pressLocked) {
		*pressed = PAD_ButtonsDown(0);
		if ((*pressed) != 0) {
			pressLocked = true;
			pressedTimer = gettime();
		}
	} else {
		if (ticks_to_millisecs(gettime() - pressedTimer) > 32) {
			pressLocked = false;
		}
	}

	*held = PAD_ButtonsHeld(0);

	// read stick position if not locked
	if (oState != POST_INPUT_LOCK) {
		x = PAD_StickX(0);
		y = PAD_StickY(0);
		// we're already recording an input
		if (stickMove) {
			data.data[data.endPoint].ax = x;
			data.data[data.endPoint].ay = y;
			data.data[data.endPoint].timeDiffUs = ticks_to_microsecs(sampleCallbackTick - prevSampleCallbackTick);
			data.endPoint++;
			// are we close to the origin?
			if ((x < STICK_MOVEMENT_THRESHOLD && x > -STICK_MOVEMENT_THRESHOLD) &&
					(y < STICK_MOVEMENT_THRESHOLD && y > -STICK_MOVEMENT_THRESHOLD)) {
				timeStickInOrigin += (ticks_to_microsecs(sampleCallbackTick - prevSampleCallbackTick));
			} else {
				timeStickInOrigin = 0;
			}
			if (data.endPoint == WAVEFORM_SAMPLES || (timeStickInOrigin / 1000) >= 40) {
				data.isDataReady = true;
				stickMove = false;
				display = true;
				oState = POST_INPUT_LOCK;
				stickCooldown = MEASURE_COOLDOWN_FRAMES;
			}
			// we've not recorded an input yet
		} else {
			// does the stick move outside the threshold?
			if ((x > STICK_MOVEMENT_THRESHOLD || x < -STICK_MOVEMENT_THRESHOLD) ||
					(y > STICK_MOVEMENT_THRESHOLD) || (y < -STICK_MOVEMENT_THRESHOLD)) {
				stickMove = true;
				data.data[0].ax = x;
				data.data[0].ay = y;
				data.data[0].timeDiffUs = ticks_to_microsecs(sampleCallbackTick - prevSampleCallbackTick);
				data.endPoint = 1;
				oState = PRE_INPUT;
			}
		}
	}
}

static void printInstructions(void *currXfb) {
	setCursorPos(2, 0);
	printStr("Press X to cycle the current test, results will show above the waveform. "
	         "Use DPAD left/right to scroll waveform when it is\nlarger than the "
	         "displayed area, hold R to move faster.", currXfb);
	printStr("\n\nCURRENT TEST: ", currXfb);
	switch (currentTest) {
		case SNAPBACK:
			printStr("SNAPBACK\nCheck the min/max value on a given axis depending on where\nyour "
					"stick started. If you moved the stick left, check the\nMax value on a given "
					"axis. Snapback can occur when the\nmax value is at or above 23. If right, "
					"then at or below -23.", currXfb);
			break;
		case PIVOT:
			printStr("PIVOT\nFor a successful pivot, you want the stick's position to stay "
				   "above/below +64/-64 for ~16.6ms (1 frame). Less, and you might get nothing, "
				   "more, and you might get a dashback. You also need the stick to hit 80/-80 on "
				   "both sides.\nCheck the PhobVision docs for more info.", currXfb);
			break;
		case DASHBACK:
			printStr("DASHBACK\nA (vanilla) dashback will be successful when the stick doesn't get "
			"polled between 23 and 64, or -23 and -64.\nLess time in this range is better.", currXfb);
			break;
		default:
			printStr("NO TEST SELECTED", currXfb);
			break;
	}
	if (!buttonLock) {
		if (*pressed & PAD_TRIGGER_Z) {
			state = OSC_POST_SETUP;
			buttonLock = true;
		}
	}
}

// only run once
static void setup(u32 *p, u32 *h) {
	setSamplingRateHigh();
	pressed = p;
	held = h;
	cb = PAD_SetSamplingCallback(oscilloscopeCallback);
	state = OSC_POST_SETUP;
}

// function called from outside
void menu_oscilloscope(void *currXfb, u32 *p, u32 *h) {
	switch (state) {
		case OSC_SETUP:
			setup(p, h);
			break;
		case OSC_POST_SETUP:
			switch (oState) {
				case PRE_INPUT:
					printStr("Waiting for input.", currXfb);
					if (ellipseCounter > 20) {
						printStr(".", currXfb);
					}
					if (ellipseCounter > 40) {
						printStr(".", currXfb);
					}
					ellipseCounter++;
					if (ellipseCounter == 60) {
						ellipseCounter = 0;
					}
					break;
				case POST_INPUT_LOCK:
					// dont allow new input until cooldown elapses
					if (stickCooldown != 0) {
						stickCooldown--;
						if (stickCooldown == 0) {
							oState = POST_INPUT;
						}
					} else {
						setCursorPos(2, 28);
						printStrColor("LOCKED", currXfb, COLOR_WHITE, COLOR_BLACK);
					}
				case POST_INPUT:
					// draw guidelines based on selected test
					DrawBox(SCREEN_TIMEPLOT_START - 1, SCREEN_POS_CENTER_Y - 128, SCREEN_TIMEPLOT_START + 500, SCREEN_POS_CENTER_Y + 128, COLOR_WHITE, currXfb);
					DrawHLine(SCREEN_TIMEPLOT_START, SCREEN_TIMEPLOT_START + 500, SCREEN_POS_CENTER_Y, COLOR_GRAY, currXfb);
					if (data.isDataReady) {

						// draw guidelines based on selected test
						DrawBox(SCREEN_TIMEPLOT_START - 1, SCREEN_POS_CENTER_Y - 128, SCREEN_TIMEPLOT_START + 500, SCREEN_POS_CENTER_Y + 128, COLOR_WHITE, currXfb);
						DrawHLine(SCREEN_TIMEPLOT_START, SCREEN_TIMEPLOT_START + 500, SCREEN_POS_CENTER_Y, COLOR_GRAY, currXfb);
						// lots of the specific values are taken from:
						// https://github.com/PhobGCC/PhobGCC-doc/blob/main/For_Users/Phobvision_Guide_Latest.md
						switch (currentTest) {
							case PIVOT:
								DrawHLine(SCREEN_TIMEPLOT_START, SCREEN_TIMEPLOT_START + 500, SCREEN_POS_CENTER_Y + 64, COLOR_GREEN, currXfb);
								DrawHLine(SCREEN_TIMEPLOT_START, SCREEN_TIMEPLOT_START + 500, SCREEN_POS_CENTER_Y - 64, COLOR_GREEN, currXfb);
								setCursorPos(8, 0);
								printStr("+64", currXfb);
								setCursorPos(15, 0);
								printStr("-64", currXfb);
								break;
							case DASHBACK:
								DrawHLine(SCREEN_TIMEPLOT_START, SCREEN_TIMEPLOT_START + 500, SCREEN_POS_CENTER_Y + 64, COLOR_GREEN, currXfb);
								DrawHLine(SCREEN_TIMEPLOT_START, SCREEN_TIMEPLOT_START + 500, SCREEN_POS_CENTER_Y - 64, COLOR_GREEN, currXfb);
								setCursorPos(8, 0);
								printStr("+64", currXfb);
								setCursorPos(15, 0);
								printStr("-64", currXfb);
							case SNAPBACK:
								DrawHLine(SCREEN_TIMEPLOT_START, SCREEN_TIMEPLOT_START + 500, SCREEN_POS_CENTER_Y + 23, COLOR_GREEN, currXfb);
								DrawHLine(SCREEN_TIMEPLOT_START, SCREEN_TIMEPLOT_START + 500, SCREEN_POS_CENTER_Y - 23, COLOR_GREEN, currXfb);
								setCursorPos(10, 0);
								printStr("+23", currXfb);
								setCursorPos(13, 0);
								printStr("-23", currXfb);
							default:
								break;
						}

						int minX, minY;
						int maxX, maxY;

						if (data.endPoint < 500) {
							dataScrollOffset = 0;
						}

						int prevX = data.data[dataScrollOffset].ax;
						int prevY = data.data[dataScrollOffset].ay;

						// initialize stat values to first point
						minX = prevX;
						maxX = prevX;
						minY = prevY;
						maxY = prevY;

						int waveformPrevXPos = 0;
						int waveformXPos = waveformScaleFactor;
						u64 drawnTicksUs = 0;

						// draw 500 datapoints from the scroll offset
						for (int i = dataScrollOffset + 1; i < dataScrollOffset + 500; i++) {
							// make sure we haven't gone outside our bounds
							if (i == data.endPoint || waveformXPos >= 500) {
								break;
							}

							// y first
							DrawLine(SCREEN_TIMEPLOT_START + waveformPrevXPos, SCREEN_POS_CENTER_Y - prevY,
									SCREEN_TIMEPLOT_START + waveformXPos, SCREEN_POS_CENTER_Y - data.data[i].ay,
									COLOR_BLUE_C, currXfb);
							prevY = data.data[i].ay;
							// then x
							DrawLine(SCREEN_TIMEPLOT_START + waveformPrevXPos, SCREEN_POS_CENTER_Y - prevX,
									SCREEN_TIMEPLOT_START + waveformXPos, SCREEN_POS_CENTER_Y - data.data[i].ax,
									COLOR_RED_C, currXfb);
							prevX = data.data[i].ax;

							// update stat values
							if (minX > prevX) {
								minX = prevX;
							}
							if (maxX < prevX) {
								maxX = prevX;
							}
							if (minY > prevY) {
								minY = prevY;
							}
							if (maxY < prevY) {
								maxY = prevY;
							}

							// adding time from drawn points, to show how long the current view is
							drawnTicksUs += data.data[i].timeDiffUs;

							// update scaling factor
							waveformPrevXPos = waveformXPos;
							waveformXPos += waveformScaleFactor;
						}

						// do we have enough data to enable scrolling?
						// TODO: enable scrolling when scaled
						if (data.endPoint >= 500 ) {
							// does the user want to scroll the waveform?
							if (*held & PAD_BUTTON_RIGHT) {
								if (*held & PAD_TRIGGER_R) {
									if (dataScrollOffset + 510 < data.endPoint) {
										dataScrollOffset += 10;
									}
								} else {
									if (dataScrollOffset + 501 < data.endPoint) {
										dataScrollOffset++;
									}
								}
							} else if (*held & PAD_BUTTON_LEFT) {
								if (*held & PAD_TRIGGER_R) {
									if (dataScrollOffset - 10 >= 0) {
										dataScrollOffset -= 10;
									}
								} else {
									if (dataScrollOffset - 1 >= 0) {
										dataScrollOffset--;
									}
								}
							}
						}

						setCursorPos(3, 0);
						// total time is stored in microseconds, divide by 1000 for milliseconds
						sprintf(strBuffer, "Total: %u, %0.3f ms | Start: %d, Shown: %0.3f ms\n", data.endPoint, (data.totalTimeUs / ((float) 1000)), dataScrollOffset + 1, (drawnTicksUs / ((float) 1000)));
						printStr(strBuffer, currXfb);

						// print test data
						setCursorPos(20, 0);
						switch (currentTest) {
							case SNAPBACK:
								sprintf(strBuffer, "Min X: %04d | Min Y: %04d   |   ", minX, minY);
								printStr(strBuffer, currXfb);
								sprintf(strBuffer, "Max X: %04d | Max Y: %04d\n", maxX, maxY);
								printStr(strBuffer, currXfb);
								break;
							case PIVOT:
								bool pivotHit80 = false;
								bool prevPivotHit80 = false;
								bool leftPivotRange = false;
								bool prevLeftPivotRange = false;
								int pivotStartIndex = -1, pivotEndIndex = -1;
								int pivotStartSign = 0;
								// start from the back of the list
								for (int i = data.endPoint; i >= 0; i--) {
									// check x coordinate for +-64 (dash threshold)
									if (data.data[i].ax >= 64 || data.data[i].ax <= -64) {
										if (pivotEndIndex == -1) {
											pivotEndIndex = i;
										}
										// pivot input must hit 80 on both sides
										if (data.data[i].ax >= 80 || data.data[i].ax <= -80) {
											pivotHit80 = true;
										}
									}

									// are we outside the pivot range and have already logged data of being in range
									if (pivotEndIndex != -1 && data.data[i].ax < 64 && data.data[i].ax > -64) {
										leftPivotRange = true;
										if (pivotStartIndex == -1) {
											// need the "previous" poll since this one is out of the range
											pivotStartIndex = i + 1;
										}
										if (prevLeftPivotRange || !pivotHit80) {
											break;
										}
									}

									// look for the initial input
									if ( (data.data[i].ax >= 64 || data.data[i].ax <= -64) && leftPivotRange) {
										// used to ensure starting input is from the opposite side
										if (pivotStartSign == 0) {
											pivotStartSign = data.data[i].ax;
										}
										prevLeftPivotRange = true;
										if (data.data[i].ax >= 80 || data.data[i].ax <= -80) {
											prevPivotHit80 = true;
											break;
										}
									}
								}

								// phobvision doc says both sides need to hit 80 to succeed
								// multiplication is to ensure signs are correct
								if (prevPivotHit80 && pivotHit80 && (data.data[pivotEndIndex].ax * pivotStartSign < 0)) {
									float noTurnPercent = 0;
									float pivotPercent = 0;
									float dashbackPercent = 0;

									u64 timeInPivotRangeUs = 0;
									for (int i = pivotStartIndex; i <= pivotEndIndex; i++) {
										timeInPivotRangeUs += data.data[i].timeDiffUs;
									}

									// convert time to float in milliseconds
									float timeInPivotRangeMs = (timeInPivotRangeUs / 1000.0);

									// TODO: i think the calculation can be simplified here...
									// how many milliseconds could a poll occur that would cause a miss
									float diffFrameTimePoll = FRAME_TIME_MS - timeInPivotRangeMs;

									// negative time difference, dashback
									if (diffFrameTimePoll < 0) {
										dashbackPercent = ((diffFrameTimePoll * -1) / FRAME_TIME_MS) * 100;
										if (dashbackPercent > 100) {
											dashbackPercent = 100;
										}
										pivotPercent = 100 - dashbackPercent;
										// positive or 0 time diff, no turn
									} else {
										noTurnPercent = (diffFrameTimePoll / FRAME_TIME_MS) * 100;
										if (noTurnPercent > 100) {
											noTurnPercent = 100;
										}
										pivotPercent = 100 - noTurnPercent;
									}

									sprintf(strBuffer, "MS: %2.2f | No turn: %2.0f%% | Pivot: %2.0f%% | Dashback: %2.0f%%",
											timeInPivotRangeMs, noTurnPercent, pivotPercent, dashbackPercent);
									printStr(strBuffer, currXfb);
									//printf("\nUS Total: %llu, Start index: %d, End index: %d", timeInPivotRangeUs, pivotStartIndex, pivotEndIndex);
								} else {
									printStr("No pivot input detected.", currXfb);
								}
								break;
							case DASHBACK:
								// go forward in list
								int dashbackStartIndex = -1, dashbackEndIndex = -1;
								u64 timeInRange = 0;
								for (int i = 0; i < data.endPoint; i++) {
									// is the stick in the range
									if ((data.data[i].ax >= 23 && data.data[i].ax < 64) || (data.data[i].ax <= -23 && data.data[i].ax > -64)) {
										timeInRange += data.data[i].timeDiffUs;
										if (dashbackStartIndex == -1) {
											dashbackStartIndex = i;
										}
									} else if (dashbackStartIndex != -1) {
										dashbackEndIndex = i - 1;
										break;
									}
								}
								float dashbackPercent;
								float ucfPercent;

								if (dashbackEndIndex == -1) {
									dashbackPercent = 0;
									ucfPercent = 0;
								} else {
									// convert time in microseconds to float time in milliseconds
									float timeInRangeMs = (timeInRange / 1000.0);

									dashbackPercent = (1.0 - (timeInRangeMs / FRAME_TIME_MS)) * 100;

									// ucf dashback is a little more involved
									u64 ucfTimeInRange = timeInRange;
									for (int i = dashbackStartIndex; i <= dashbackEndIndex; i++) {
										// we're gonna assume that the previous frame polled around the origin, because i cant be bothered
										// it also makes the math easier
										u64 usFromPoll = 0;
										int nextPollIndex = i;
										// we need the sample that would occur around 1f after
										while (usFromPoll < 16666) {
											nextPollIndex++;
											usFromPoll += data.data[nextPollIndex].timeDiffUs;
										}
										// the two frames need to move more than 75 units for UCF to convert it
										if (data.data[i].ax + data.data[nextPollIndex].ax > 75 ||
												data.data[i].ax + data.data[nextPollIndex].ax < -75) {
											ucfTimeInRange -= data.data[i].timeDiffUs;
										}
									}

									float ucfTimeInRangeMs = ucfTimeInRange / 1000.0;
									if (ucfTimeInRangeMs <= 0) {
										ucfPercent = 100;
									} else {
										ucfPercent = (1.0 - (ucfTimeInRangeMs / FRAME_TIME_MS)) * 100;
									}

									// this shouldn't happen in theory, maybe on box?
									if (dashbackPercent > 100) {
										dashbackPercent = 100;
									}
									if (ucfPercent > 100) {
										ucfPercent = 100;
									}
									// this definitely can happen though
									if (dashbackPercent < 0) {
										dashbackPercent = 0;
									}
									if (ucfPercent < 0) {
										ucfPercent = 0;
									}
								}
								sprintf(strBuffer, "Vanilla Success: %2.0f%% | UCF Success: %2.0f%%", dashbackPercent, ucfPercent);
								printStr(strBuffer, currXfb);
								break;
							case NO_TEST:
								break;
							default:
								printStr("Error?", currXfb);
								break;

						}
					}
					setCursorPos(21,0);
					printStr("Current test: ", currXfb);
					switch (currentTest) {
						case SNAPBACK:
							printStr("Snapback", currXfb);
							break;
						case PIVOT:
							printStr("Pivot", currXfb);
							break;
						case DASHBACK:
							printStr("Dashback", currXfb);
							break;
						case NO_TEST:
							printStr("None", currXfb);
							break;
						default:
							printStr("Error", currXfb);
							break;
					}
					break;
				default:
					printStr("How did we get here?", currXfb);
					break;
			}
			if (!buttonLock){
				if (*pressed & PAD_BUTTON_A && !buttonLock) {
					if (oState == POST_INPUT_LOCK && stickCooldown == 0) {
						oState = POST_INPUT;
					} else {
						oState = POST_INPUT_LOCK;
					}
					buttonLock = true;
				}
				if (*pressed & PAD_BUTTON_X && !buttonLock) {
					currentTest++;
					// check if we overrun our test length
					if (currentTest == OSCILLOSCOPE_TEST_LEN) {
						currentTest = SNAPBACK;
					}
					buttonLock = true;
				}
				if (*pressed & PAD_TRIGGER_Z && !buttonLock) {
					state = OSC_INSTRUCTIONS;
					buttonLock = true;
				}
			}
			// adjust scaling factor
			//} else if (pressed & PAD_BUTTON_Y) {
			//	waveformScaleFactor++;
			//	if (waveformScaleFactor > 5) {
			//		waveformScaleFactor = 1;
			//	}
			break;
		case OSC_INSTRUCTIONS:
			printInstructions(currXfb);
			break;
		default:
			printStr("How did we get here?", currXfb);
			break;
	}
	if ((*held) == 0 && buttonLock) {
		buttonLock = false;
	}
}

void menu_oscilloscopeEnd() {
	setSamplingRateNormal();
	PAD_SetSamplingCallback(cb);
	pressed = NULL;
	held = NULL;
	state = OSC_SETUP;
}
