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
package com.csipsimple.models;

public class DownloadProgress {
	private final long mDownloaded;
	private final int mTotal;

	public DownloadProgress(long downloaded, int total) {
		mDownloaded = downloaded;
		mTotal = total;
	}

	public long getDownloaded() {
		return mDownloaded;
	}

	public int getTotal() {
		return mTotal;
	}
}
