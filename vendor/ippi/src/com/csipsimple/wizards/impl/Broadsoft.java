/**
 * Copyright (C) 2010 Regis Montoya (aka r3gis - www.r3gis.fr)
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
package com.csipsimple.wizards.impl;

import java.util.HashMap;

import android.net.Uri;
import android.preference.EditTextPreference;
import android.text.TextUtils;

import fr.ippi.voip.app.R;
import com.csipsimple.api.SipProfile;

public abstract class Broadsoft extends BaseImplementation {
	protected EditTextPreference accountDisplayName;
	protected EditTextPreference accountUsername;
	protected EditTextPreference accountAuthorization;
	protected EditTextPreference accountPassword;
	protected EditTextPreference accountServer;
	protected EditTextPreference accountProxy;
	
	protected static String DISPLAY_NAME = "display_name";
	protected static String USER_NAME = "username";
	protected static String AUTH_NAME = "auth_name";
	protected static String PASSWORD = "password";
	protected static String SERVER = "server";
	protected static String PROXY = "proxy";

	private void bindFields() {
		accountDisplayName = (EditTextPreference) parent.findPreference(DISPLAY_NAME);
		accountUsername = (EditTextPreference) parent.findPreference(USER_NAME);
		accountAuthorization = (EditTextPreference) parent.findPreference(AUTH_NAME);
		accountPassword = (EditTextPreference) parent.findPreference(PASSWORD);
		accountServer = (EditTextPreference) parent.findPreference(SERVER);
		accountProxy = (EditTextPreference) parent.findPreference(PROXY);
	}
	
	
	
	public void fillLayout(final SipProfile account) {
		bindFields();
		if(!TextUtils.isEmpty(account.getDisplayName())) {
			accountDisplayName.setText(account.getDisplayName());
		}else {
			accountDisplayName.setText(getDefaultName());
		}
		
		accountUsername.setText(account.getSipUserName());
		accountServer.setText(account.getSipDomain());
		
		accountPassword.setText(account.data);
		accountAuthorization.setText(account.username);
		accountProxy.setText(account.getProxyAddress().replaceFirst("sip:", ""));
	}

	public void updateDescriptions() {
		setStringFieldSummary(DISPLAY_NAME);
		setStringFieldSummary(USER_NAME);
		setPasswordFieldSummary(PASSWORD);
		setStringFieldSummary(AUTH_NAME);
		setStringFieldSummary(SERVER);
		setStringFieldSummary(PROXY);
		
	}
	
	private static HashMap<String, Integer>SUMMARIES = new  HashMap<String, Integer>(){

	/**
		 * 
		 */
		private static final long serialVersionUID = 8886964429916862979L;

	{
		put(DISPLAY_NAME, R.string.w_common_display_name_desc);
		put(USER_NAME, R.string.w_authorization_phone_number_desc);
		put(AUTH_NAME, R.string.w_authorization_auth_name_desc);
		put(PASSWORD, R.string.w_common_password_desc);
		put(SERVER, R.string.w_common_server_desc);
		put(PROXY, R.string.w_advanced_proxy_desc);
	}};
	
	@Override
	public String getDefaultFieldSummary(String fieldName) {
		Integer res = SUMMARIES.get(fieldName);
		if(res != null) {
			return parent.getString( res );
		}
		return "";
	}

	public boolean canSave() {
		boolean isValid = true;
		
		isValid &= checkField(accountDisplayName, isEmpty(accountDisplayName));
		isValid &= checkField(accountUsername, isEmpty(accountUsername));
		isValid &= checkField(accountAuthorization, isEmpty(accountAuthorization));
		isValid &= checkField(accountPassword, isEmpty(accountPassword));
		isValid &= checkField(accountServer, isEmpty(accountServer));
		isValid &= checkField(accountProxy, isEmpty(accountProxy));

		return isValid;
	}

	public SipProfile buildAccount(SipProfile account) {
		account.display_name = accountDisplayName.getText();
		account.acc_id = "<sip:" + Uri.encode(accountUsername.getText().trim()) + "@" + getDomain() + ">";
		
		String regUri = "sip:" + getDomain();
		account.reg_uri = regUri;
		account.proxies = new String[] { "sip:"+accountProxy.getText() } ;

		account.realm = "*";
		account.username = getText(accountAuthorization).trim();
		account.data = getText(accountPassword);
		account.scheme = SipProfile.CRED_SCHEME_DIGEST;
		account.datatype = SipProfile.CRED_DATA_PLAIN_PASSWD;
		account.reg_timeout = 1800;
		account.transport = SipProfile.TRANSPORT_UDP;
		return account;
	}

	protected String getDomain() {
		return accountServer.getText();
	}
	
	
	protected abstract String getDefaultName();

	@Override
	public int getBasePreferenceResource() {
		return R.xml.w_auth_and_proxy_preferences;
	}

	@Override
	public boolean needRestart() {
		return false;
	}

}
