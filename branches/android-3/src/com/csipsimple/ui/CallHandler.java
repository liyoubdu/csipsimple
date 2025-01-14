/**
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
package com.csipsimple.ui;

import java.util.regex.Matcher;
import java.util.regex.Pattern;

import org.pjsip.pjsua.pjsip_inv_state;
import org.pjsip.pjsua.pjsip_status_code;
import org.pjsip.pjsua.pjsua;

import android.app.Activity;
import android.app.KeyguardManager;
import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.ServiceConnection;
import android.os.Bundle;
import android.os.IBinder;
import android.os.PowerManager;
import android.os.PowerManager.WakeLock;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.csipsimple.R;
import com.csipsimple.models.CallInfo;
import com.csipsimple.service.ISipService;
import com.csipsimple.service.SipService;
import com.csipsimple.service.UAStateReceiver;

public class CallHandler extends Activity {
	private static String THIS_FILE = "SIP CALL HANDLER";

	/**
	 * Service binding
	 */
	private boolean m_servicedBind = false;
	private ServiceConnection m_connection = new ServiceConnection() {

		@Override
		public void onServiceConnected(ComponentName arg0, IBinder arg1) {
			ISipService.Stub.asInterface(arg1);
		}

		@Override
		public void onServiceDisconnected(ComponentName arg0) {

		}

	};

	private CallInfo mCallInfo = null;
	private Button mTakeCall;
	private Button mClearCall;
	private TextView mRemoteContact;
	private LinearLayout mMainFrame;

	private WakeLock wl;
    private KeyguardManager mKeyguardManager;
    private KeyguardManager.KeyguardLock mKeyguardLock;


	@Override
	public void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);

		setContentView(R.layout.callhandler);
		Log.d(THIS_FILE, "Creating call handler.....");
		m_servicedBind = bindService(new Intent(this, SipService.class), m_connection, Context.BIND_AUTO_CREATE);

		Bundle extras = getIntent().getExtras();
		if (extras != null) {
			mCallInfo = (CallInfo) extras.get("call_info");
		}

		if (mCallInfo == null) {
			Log.e(THIS_FILE, "You provide an empty call info....");
			finish();
		}

		Log.d(THIS_FILE, "Creating call handler for " + mCallInfo.getCallId());

		registerReceiver(callStateReceiver, new IntentFilter(UAStateReceiver.UA_CALL_STATE_CHANGED));

		mTakeCall = (Button) findViewById(R.id.take_call);
		mTakeCall.setOnClickListener(new View.OnClickListener() {
			public void onClick(View v) {
				// TODO : should be done into the service
				if (mCallInfo != null) {
					pjsua.call_answer(mCallInfo.getCallId(), pjsip_status_code.PJSIP_SC_OK.swigValue(), null, null);
				}
			}
		});

		mClearCall = (Button) findViewById(R.id.hangup);
		mClearCall.setOnClickListener(new View.OnClickListener() {
			public void onClick(View v) {
				// TODO : should be done into the service
				if (mCallInfo != null) {
					pjsua.call_hangup(mCallInfo.getCallId(), 0, null, null);
				}

			}
		});

		mRemoteContact = (TextView) findViewById(R.id.remoteContact);
		mMainFrame = (LinearLayout) findViewById(R.id.mainFrame);
		updateUIFromCall();

	}
	
	private boolean manage_keyguard = false;
	
	@Override
	protected void onStart() {
		super.onStart();
        if (mKeyguardManager == null) {
            mKeyguardManager = (KeyguardManager) getSystemService(Context.KEYGUARD_SERVICE);
            mKeyguardLock = mKeyguardManager.newKeyguardLock("com.csipsimple.inCallKeyguard");
        }
        if(mKeyguardManager.inKeyguardRestrictedInputMode()) {
        	manage_keyguard = true;
        	mKeyguardLock.disableKeyguard();
        }
	}
	
	@Override
	protected void onStop() {
		super.onStop();
		if(manage_keyguard) {
			mKeyguardLock.reenableKeyguard();
		}

	}

	private void updateUIFromCall() {

		Log.d(THIS_FILE, "Update ui from call " + mCallInfo.getCallId() + " state " + mCallInfo.getStringCallState());

		pjsip_inv_state state = mCallInfo.getCallState();
		String remote_contact = mCallInfo.getRemoteContact();
		Pattern p = Pattern.compile("^(?:\")?([^<\"]*)(?:\")?[ ]*<sip(?:s)?:([^@]*@[^>]*)>");
		Matcher m = p.matcher(remote_contact);
		if (m.matches()) {
			remote_contact = m.group(1);
			if(remote_contact == null || remote_contact.equalsIgnoreCase("")) {
				remote_contact = m.group(2);
			}
		}
		
		mRemoteContact.setText(remote_contact);

		int backgroundResId = R.drawable.bg_in_call_gradient_unidentified;
        if (wl == null) {
            PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
            wl = pm.newWakeLock(PowerManager.SCREEN_BRIGHT_WAKE_LOCK | PowerManager.ACQUIRE_CAUSES_WAKEUP | PowerManager.ON_AFTER_RELEASE, "com.csipsimple.onIncomingCall");
        }


		switch (state) {
		case PJSIP_INV_STATE_INCOMING:
			mTakeCall.setVisibility(View.VISIBLE);
			mClearCall.setVisibility(View.VISIBLE);
			mClearCall.setText("Decline");
		    wl.acquire();
			break;
		case PJSIP_INV_STATE_CALLING:
			mTakeCall.setVisibility(View.GONE);
			mClearCall.setVisibility(View.VISIBLE);
			mClearCall.setText("Hang up");
            if (wl != null && wl.isHeld()) {
                wl.release();
            }
			break;
		case PJSIP_INV_STATE_CONFIRMED:
			mTakeCall.setVisibility(View.GONE);
			mClearCall.setVisibility(View.VISIBLE);
			mClearCall.setText("Hang up");
			backgroundResId = R.drawable.bg_in_call_gradient_connected;
			if (wl != null && wl.isHeld()) {
                wl.release();
            }
			break;
		case PJSIP_INV_STATE_NULL:
			Log.i(THIS_FILE, "WTF?");
			if (wl != null && wl.isHeld()) {
                wl.release();
            }
			break;
		case PJSIP_INV_STATE_DISCONNECTED:
			Log.i(THIS_FILE, "Disconnected here !!!");
			
			finish();
			return;
		case PJSIP_INV_STATE_EARLY:
		case PJSIP_INV_STATE_CONNECTING:
			break;
		}

		mMainFrame.setBackgroundResource(backgroundResId);
	}

	@Override
	protected void onDestroy() {
		if (m_servicedBind) {
			unbindService(m_connection);
		}
		if (wl != null && wl.isHeld()) {
            wl.release();
        }

		unregisterReceiver(callStateReceiver);

		super.onDestroy();
	}

	private BroadcastReceiver callStateReceiver = new BroadcastReceiver() {
		public void onReceive(Context context, Intent intent) {
			Bundle extras = intent.getExtras();
			CallInfo notif_call = null;
			if (extras != null) {
				notif_call = (CallInfo) extras.get("call_info");
			}

			Log.d(THIS_FILE, "BC recieve");

			if (notif_call != null && mCallInfo.equals(notif_call)) {
				mCallInfo = notif_call;
				updateUIFromCall();
			}
		}
	};
}
