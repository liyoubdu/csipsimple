/**
 * Copyright (C) 2010 Regis Montoya (aka r3gis - www.r3gis.fr)
 * Copyright (C) 2008 The Android Open Source Project
 * 
 * This file is part of CSipSimple.
 *
 *  CSipSimple is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  CSipSimple is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with CSipSimple.  If not, see <http://www.gnu.org/licenses/>.
 */
/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.csipsimple.theme.eclair.widgets;

import android.content.Context;
import android.graphics.Rect;
import android.os.Vibrator;
import android.util.AttributeSet;
import android.util.Log;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.TextView;
import android.widget.ImageView.ScaleType;

import com.csipsimple.theme.eclair.R;

/**
 * A special widget containing two Sliders and a threshold for each. Moving
 * either slider beyond the threshold will cause the registered
 * OnTriggerListener.onTrigger() to be called with
 * {@link OnTriggerListener#LEFT_HANDLE} or
 * {@link OnTriggerListener#RIGHT_HANDLE} to be called.
 * Deeply inspired from android SlidingTab internal widget but simplified for our use
 * 
 */
public class SlidingTab extends ViewGroup {

	private static final float TARGET_ZONE = 2.0f / 3.0f;
	private static final long VIBRATE_SHORT = 30;
	private static final long VIBRATE_LONG = 40;

	private OnTriggerListener onTriggerListener;
	private boolean triggered = false;
	private Vibrator mVibrator;
	// used to scale dimensions for bitmaps.
	private float density; 


	private Slider leftSlider, rightSlider, currentSlider;
	private boolean tracking;
	private float targetZone;
	private static final String THIS_FILE = "SlidingTab";

	/**
	 * Interface definition for a callback to be invoked when a tab is triggered
	 * by moving it beyond a target zone.
	 */
	public interface OnTriggerListener {

		/**
		 * The interface was triggered because the user grabbed the left handle
		 * and moved it past the target zone.
		 */
		public static final int LEFT_HANDLE = 0;

		/**
		 * The interface was triggered because the user grabbed the right handle
		 * and moved it past the target zone.
		 */
		public static final int RIGHT_HANDLE = 1;

		/**
		 * Called when the user moves a handle beyond the target zone.
		 * 
		 * @param v
		 *            The view that was triggered.
		 * @param whichHandle
		 *            Which "dial handle" the user grabbed, either
		 *            {@link #LEFT_HANDLE}, {@link #RIGHT_HANDLE}.
		 */
		void onTrigger(View v, int whichHandle);
	}

	/**
	 * Simple container class for all things pertinent to a slider. A slider
	 * consists of 3 Views:
	 * 
	 * {@link #tab} is the tab shown on the screen in the default state.
	 * {@link #text} is the view revealed as the user slides the tab out.
	 * {@link #target} is the target the user must drag the slider past to
	 * trigger the slider.
	 * 
	 */
	private static class Slider {
		/**
		 * Tab alignment - determines which side the tab should be drawn on
		 */
		public static final int ALIGN_LEFT = 0;
		public static final int ALIGN_RIGHT = 1;

		/**
		 * States for the view.
		 */
		private static final int STATE_NORMAL = 0;
		private static final int STATE_PRESSED = 1;
		private static final int STATE_ACTIVE = 2;

		private final ImageView tab;
		private final TextView text;
		private final ImageView target;

		/**
		 * Constructor
		 * 
		 * @param parent
		 *            the container view of this one
		 * @param tabId
		 *            drawable for the tab
		 * @param barId
		 *            drawable for the bar
		 * @param targetId
		 *            drawable for the target
		 */
		Slider(ViewGroup parent, int iconId, int targetId, int barId, int tabId) {
			// Create tab
			tab = new ImageView(parent.getContext());
			tab.setBackgroundResource(tabId);
			tab.setImageResource(iconId);
			tab.setScaleType(ScaleType.CENTER);
			tab.setLayoutParams(new LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT));

			// Create hint TextView
			text = new TextView(parent.getContext());
			text.setLayoutParams(new LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.FILL_PARENT));
			text.setBackgroundResource(barId);
			text.setTextAppearance(parent.getContext(), R.style.TextAppearance_SlidingTabNormal);

			// Create target
			target = new ImageView(parent.getContext());
			target.setImageResource(targetId);
			target.setScaleType(ScaleType.CENTER);
			target.setLayoutParams(new LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT));
			target.setVisibility(View.INVISIBLE);
			
			
			// this needs to be first - relies on painter's algorithm
			parent.addView(target); 
			parent.addView(tab);
			parent.addView(text);
		}
		
		void setResources(int iconId, int targetId, int barId, int tabId) {
			tab.setImageResource(iconId);
			tab.setBackgroundResource(tabId);
			text.setBackgroundResource(barId);
			target.setImageResource(targetId);
		}
		

		void setHintText(int resId) {
			text.setText(resId);
		}

		void hide() {
			text.setVisibility(View.INVISIBLE);
			tab.setVisibility(View.INVISIBLE);
			target.setVisibility(View.INVISIBLE);
		}

		void setState(int state) {
			text.setPressed(state == STATE_PRESSED);
			tab.setPressed(state == STATE_PRESSED);
			if (state == STATE_ACTIVE) {
				final int[] activeState = new int[] { android.R.attr.state_active };
				if (text.getBackground().isStateful()) {
					text.getBackground().setState(activeState);
				}
				if (tab.getBackground().isStateful()) {
					tab.getBackground().setState(activeState);
				}
				text.setTextAppearance(text.getContext(), R.style.TextAppearance_SlidingTabActive);
			} else {
				text.setTextAppearance(text.getContext(), R.style.TextAppearance_SlidingTabNormal);
			}
		}

		void showTarget() {
			target.setVisibility(View.VISIBLE);
		}

		void reset() {
			setState(STATE_NORMAL);
			text.setVisibility(View.VISIBLE);
			text.setTextAppearance(text.getContext(), R.style.TextAppearance_SlidingTabNormal);
			tab.setVisibility(View.VISIBLE);
			target.setVisibility(View.INVISIBLE);
		}


		/**
		 * Layout the given widgets within the parent.
		 * 
		 * @param l
		 *            the parent's left border
		 * @param t
		 *            the parent's top border
		 * @param r
		 *            the parent's right border
		 * @param b
		 *            the parent's bottom border
		 * @param alignment
		 *            which side to align the widget to
		 */
		void layout(int l, int t, int r, int b, int alignment) {
			final int handleWidth = tab.getBackground().getIntrinsicWidth();
			final int handleHeight = tab.getBackground().getIntrinsicHeight();
			final int targetWidth = target.getDrawable().getIntrinsicWidth();
			final int targetHeight = target.getDrawable().getIntrinsicHeight();
			final int parentWidth = r - l;
			final int parentHeight = b - t;

			final int leftTarget = (int) (TARGET_ZONE * parentWidth) - targetWidth + handleWidth / 2;
			final int rightTarget = (int) ((1.0f - TARGET_ZONE) * parentWidth) - handleWidth / 2;

			final int targetTop = (parentHeight - targetHeight) / 2;
			final int targetBottom = targetTop + targetHeight;
			final int top = (parentHeight - handleHeight) / 2;
			final int bottom = (parentHeight + handleHeight) / 2;
			if (alignment == ALIGN_LEFT) {
				tab.layout(0, top, handleWidth, bottom);
				text.layout(0 - parentWidth, top, 0, bottom);
				text.setGravity(Gravity.RIGHT);
				target.layout(leftTarget, targetTop, leftTarget + targetWidth, targetBottom);
			} else {
				tab.layout(parentWidth - handleWidth, top, parentWidth, bottom);
				text.layout(parentWidth, top, parentWidth + parentWidth, bottom);
				target.layout(rightTarget, targetTop, rightTarget + targetWidth, targetBottom);
				text.setGravity(Gravity.TOP);
			}
			
		}

		public int getTabWidth() {
			return tab.getBackground().getIntrinsicWidth();
		}

		public int getTabHeight() {
			return tab.getBackground().getIntrinsicHeight();
		}
	}

	public SlidingTab(Context context) {
		this(context, null);
	}

	/**
	 * Constructor used when this widget is created from a layout file.
	 */
	public SlidingTab(Context context, AttributeSet attrs) {
		super(context, attrs);

		density = getResources().getDisplayMetrics().density;
		leftSlider = new Slider(this, R.drawable.ic_jog_dial_answer, R.drawable.jog_tab_target_green, R.drawable.jog_tab_bar_left_answer, R.drawable.jog_tab_left_answer);
		rightSlider = new Slider(this, R.drawable.ic_jog_dial_decline, R.drawable.jog_tab_target_red, R.drawable.jog_tab_bar_right_decline, R.drawable.jog_tab_right_decline);
	}

	@Override
	protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
		int widthSpecMode = MeasureSpec.getMode(widthMeasureSpec);
		int widthSpecSize = MeasureSpec.getSize(widthMeasureSpec);

		int heightSpecMode = MeasureSpec.getMode(heightMeasureSpec);

		if (widthSpecMode == MeasureSpec.UNSPECIFIED || heightSpecMode == MeasureSpec.UNSPECIFIED) {
			throw new RuntimeException("Sliding tab cannot have UNSPECIFIED dimensions");
		}

		final int leftTabWidth = (int) (density * leftSlider.getTabWidth() + 0.5f);
		final int rightTabWidth = (int) (density * rightSlider.getTabWidth() + 0.5f);
		final int leftTabHeight = (int) (density * leftSlider.getTabHeight() + 0.5f);
		final int rightTabHeight = (int) (density * rightSlider.getTabHeight() + 0.5f);
		final int width;
		final int height;

		width = Math.max(widthSpecSize, leftTabWidth + rightTabWidth);
		height = Math.max(leftTabHeight, rightTabHeight);
		Log.d(THIS_FILE, "Heights are : "+leftTabHeight+" and "+rightTabHeight+" density "+density);
		setMeasuredDimension(width, height);
	}

	@Override
	public boolean onInterceptTouchEvent(MotionEvent event) {
		final int action = event.getAction();
		final float x = event.getX();
		final float y = event.getY();
		final Rect frame = new Rect();

		View leftHandle = leftSlider.tab;
		leftHandle.getHitRect(frame);
		boolean leftHit = frame.contains((int) x, (int) y);

		View rightHandle = rightSlider.tab;
		rightHandle.getHitRect(frame);
		boolean rightHit = frame.contains((int) x, (int) y);

		if (!tracking && !(leftHit || rightHit)) {
			return false;
		}

		switch (action) {
		case MotionEvent.ACTION_DOWN: {
			tracking = true;
			triggered = false;
			vibrate(VIBRATE_SHORT);
			if (leftHit) {
				currentSlider = leftSlider;
				targetZone = TARGET_ZONE;
				rightSlider.hide();
			} else {
				currentSlider = rightSlider;
				targetZone = 1.0f - TARGET_ZONE;
				leftSlider.hide();
			}
			currentSlider.setState(Slider.STATE_PRESSED);
			currentSlider.showTarget();
			break;
		}
		}

		return true;
	}
	

	@Override
	public boolean onTouchEvent(MotionEvent event) {
		if (tracking) {
			final int action = event.getAction();
			final float x = event.getX();
			final float y = event.getY();
			final View handle = currentSlider.tab;
			switch (action) {
			case MotionEvent.ACTION_MOVE:
				moveHandle(x, y);
				float position = x;
				float target = targetZone * getWidth();
				boolean targetZoneReached = (currentSlider == leftSlider ? position > target : position < target);

				if (!triggered && targetZoneReached) {
					triggered = true;
					tracking = false;
					currentSlider.setState(Slider.STATE_ACTIVE);
					dispatchTriggerEvent(currentSlider == leftSlider ? OnTriggerListener.LEFT_HANDLE : OnTriggerListener.RIGHT_HANDLE);
				}

				if (y <= handle.getBottom() && y >= handle.getTop()) {
					break;
				}
				// Intentionally fall through - we're outside tracking rectangle
			case MotionEvent.ACTION_UP:
			case MotionEvent.ACTION_CANCEL:
				tracking = false;
				triggered = false;
				resetView();
				break;
			}
		}

		return tracking || super.onTouchEvent(event);
	}
	
	

	private void resetView() {
		leftSlider.reset();
		rightSlider.reset();
		onLayout(true, getLeft(), getTop(), getLeft() + getWidth(), getTop() + getHeight());
	}

	@Override
	protected void onLayout(boolean changed, int l, int t, int r, int b) {
		if (!changed) {
			return;
		}

		// Center the widgets in the view
		leftSlider.layout(l, t, r, b, Slider.ALIGN_LEFT);
		rightSlider.layout(l, t, r, b, Slider.ALIGN_RIGHT);

		invalidate();
	}

	private void moveHandle(float x, float y) {
		final View handle = currentSlider.tab;
		final View content = currentSlider.text;

		int deltaX = (int) x - handle.getLeft() - (handle.getWidth() / 2);
		handle.offsetLeftAndRight(deltaX);
		content.offsetLeftAndRight(deltaX);

		invalidate();
	}

	/**
	 * Sets the left handle icon to a given resource.
	 * 
	 * The resource should refer to a Drawable object, or use 0 to remove the
	 * icon.
	 * 
	 * @param iconId
	 *            the resource ID of the icon drawable
	 * @param targetId
	 *            the resource of the target drawable
	 * @param barId
	 *            the resource of the bar drawable (stateful)
	 * @param tabId
	 *            the resource of the
	 */
	public void setLeftTabResources(int iconId, int targetId, int barId, int tabId) {
		leftSlider.setResources(iconId, targetId, barId, tabId);
	}

	/**
	 * Sets the left handle hint text to a given resource string.
	 * 
	 * @param resId
	 */
	public void setLeftHintText(int resId) {
		leftSlider.setHintText(resId);
	}

	/**
	 * Sets the right handle icon to a given resource.
	 * 
	 * The resource should refer to a Drawable object, or use 0 to remove the
	 * icon.
	 * 
	 * @param iconId
	 *            the resource ID of the icon drawable
	 * @param targetId
	 *            the resource of the target drawable
	 * @param barId
	 *            the resource of the bar drawable (stateful)
	 * @param tabId
	 *            the resource of the
	 */
	public void setRightTabResources(int iconId, int targetId, int barId, int tabId) {
		rightSlider.setResources(iconId, targetId, barId, tabId);
	}

	/**
	 * Sets the left handle hint text to a given resource string.
	 * @param resId
	 */
	public void setRightHintText(int resId) {
		rightSlider.setHintText(resId);
	}

	/**
	 * Triggers haptic feedback.
	 */
	private synchronized void vibrate(long duration) {
		if (mVibrator == null) {
			mVibrator = (android.os.Vibrator) getContext().getSystemService(Context.VIBRATOR_SERVICE);
		}
		mVibrator.vibrate(duration);
	}

	/**
	 * Registers a callback to be invoked when the user triggers an event.
	 * 
	 * @param listener
	 *            the OnDialTriggerListener to attach to this view
	 */
	public void setOnTriggerListener(OnTriggerListener listener) {
		onTriggerListener = listener;
	}

	/**
	 * Dispatches a trigger event to listener. Ignored if a listener is not set.
	 * 
	 * @param whichHandle
	 *            the handle that triggered the event.
	 */
	private void dispatchTriggerEvent(int whichHandle) {
		vibrate(VIBRATE_LONG);
		Log.d(THIS_FILE, "We take the call....");
		if (onTriggerListener != null) {
			Log.d(THIS_FILE, "We transmit to the parent....");
			onTriggerListener.onTrigger(this, whichHandle);
		}
	}


}
