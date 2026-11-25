-- ===========================================================
-- Title		:	usp_TryWarehouseRegistration
-- Author		:	ÀÌÃ¢¿ë / Chang-yong Lee (sibukano@ncsoft.com)
-- Create date	:	2013-07-12
-- Description	:	
-- ===========================================================
CREATE PROCEDURE dbo.usp_TryWarehouseRegistration
(
	@NewLabelID				BIGINT		OUTPUT,
	@OwnerAccountID			UNIQUEIDENTIFIER,
	@GoodsID				BIGINT,
	@GoodsNumber			INT,
	@SenderDescription		NVARCHAR(128),
	@SenderMessage			NVARCHAR(128),
	@PurchaseTime			DATETIME,
	-- Item 1
	@GoodsItemNumber_1		INT,
	@ItemDataID_1			INT,
	@ItemAmount_1			INT,
	@UsableDuration_1		INT,
	-- Item 2
	@GoodsItemNumber_2		INT		= NULL,
	@ItemDataID_2			INT		= NULL,
	@ItemAmount_2			INT		= NULL,
	@UsableDuration_2		INT		= NULL,
	-- Item 3
	@GoodsItemNumber_3		INT		= NULL,
	@ItemDataID_3			INT		= NULL,
	@ItemAmount_3			INT		= NULL,
	@UsableDuration_3		INT		= NULL,
	-- Item 4
	@GoodsItemNumber_4		INT		= NULL,
	@ItemDataID_4			INT		= NULL,
	@ItemAmount_4			INT		= NULL,
	@UsableDuration_4		INT		= NULL,
	-- Item 5
	@GoodsItemNumber_5		INT		= NULL,
	@ItemDataID_5			INT		= NULL,
	@ItemAmount_5			INT		= NULL,
	@UsableDuration_5		INT		= NULL,
	-- Item 6
	@GoodsItemNumber_6		INT		= NULL,
	@ItemDataID_6			INT		= NULL,
	@ItemAmount_6			INT		= NULL,
	@UsableDuration_6		INT		= NULL,
	-- Item 7
	@GoodsItemNumber_7		INT		= NULL,
	@ItemDataID_7			INT		= NULL,
	@ItemAmount_7			INT		= NULL,
	@UsableDuration_7		INT		= NULL,
	-- Item 8
	@GoodsItemNumber_8		INT		= NULL,
	@ItemDataID_8			INT		= NULL,
	@ItemAmount_8			INT		= NULL,
	@UsableDuration_8		INT		= NULL,
	-- Item 9
	@GoodsItemNumber_9		INT		= NULL,
	@ItemDataID_9			INT		= NULL,
	@ItemAmount_9			INT		= NULL,
	@UsableDuration_9		INT		= NULL,
	-- Item 10
	@GoodsItemNumber_10		INT		= NULL,
	@ItemDataID_10			INT		= NULL,
	@ItemAmount_10			INT		= NULL,
	@UsableDuration_10		INT		= NULL
)
AS
BEGIN
	SET NOCOUNT, XACT_ABORT ON;
	
	SET	@NewLabelID	= 0;
	
	DECLARE	@INIT_TRANCOUNT		INT	= @@TRANCOUNT,
			-- Error Code
			@ERR_SUCCESS		INT = 0,
			@ERR_FAILURE		INT = 1;
	
	BEGIN TRY
		-- Transaction validity check
		IF	(@@NESTLEVEL = 1 AND @INIT_TRANCOUNT > 0)
			RAISERROR(N'invalid initial transaction, init trancount:%d', 16, @ERR_FAILURE, @INIT_TRANCOUNT);
		
		DECLARE	@ownerAccountIdStr	NCHAR(36) = CAST(@OwnerAccountID AS NCHAR(36)),
				@purchaseTimeStr	NCHAR(19) = CONVERT(NCHAR(19), @PurchaseTime, 120);
		-- Parameter validity check
		IF	(	@OwnerAccountID IS NULL
				OR	@GoodsID IS NULL		-- ¿ÜºÎ Á¤ÀÇ
				OR	@GoodsNumber IS NULL	-- ¿ÜºÎ Á¤ÀÇ
				OR	(@SenderDescription IS NOT NULL AND @SenderDescription = N'')
				OR	@PurchaseTime IS NULL	)
			RAISERROR(N'invalid parameter, ownerAccountId:%s, goodsId:0x%I64X, goodsNumber:%d, senderDescription:%s, purchaseTime:%s', 16, @ERR_FAILURE,
											@ownerAccountIdStr, @GoodsID, @GoodsNumber, @SenderDescription, @purchaseTimeStr);
		
		DECLARE	@warehouseItem	TABLE
		(
			GoodsItemNumber		INT		NOT NULL,	-- ¿ÜºÎ Á¤ÀÇ
			ItemDataID			INT		NOT NULL	CHECK	(ItemDataID > 0),
			ItemAmount			INT		NOT NULL	CHECK	(ItemAmount > 0),
			UsableDuration		INT		NULL		CHECK	(UsableDuration > 0)
		);
		
		DECLARE	@itemCount			INT			= 0,
				@resultCount		INT			= 0,
				@registrationTime	DATETIME	= GETDATE();
		
		INSERT INTO @warehouseItem (
			GoodsItemNumber,
			ItemDataID,
			ItemAmount,
			UsableDuration
		)
		SELECT	GoodsItemNumber,
				ItemDataID,
				ItemAmount,
				UsableDuration
		FROM	(
					VALUES
					(@GoodsItemNumber_1,	@ItemDataID_1,	@ItemAmount_1,	@UsableDuration_1),
					(@GoodsItemNumber_2,	@ItemDataID_2,	@ItemAmount_2,	@UsableDuration_2),
					(@GoodsItemNumber_3,	@ItemDataID_3,	@ItemAmount_3,	@UsableDuration_3),
					(@GoodsItemNumber_4,	@ItemDataID_4,	@ItemAmount_4,	@UsableDuration_4),
					(@GoodsItemNumber_5,	@ItemDataID_5,	@ItemAmount_5,	@UsableDuration_5),
					(@GoodsItemNumber_6,	@ItemDataID_6,	@ItemAmount_6,	@UsableDuration_6),
					(@GoodsItemNumber_7,	@ItemDataID_7,	@ItemAmount_7,	@UsableDuration_7),
					(@GoodsItemNumber_8,	@ItemDataID_8,	@ItemAmount_8,	@UsableDuration_8),
					(@GoodsItemNumber_9,	@ItemDataID_9,	@ItemAmount_9,	@UsableDuration_9),
					(@GoodsItemNumber_10,	@ItemDataID_10,	@ItemAmount_10,	@UsableDuration_10)
				)	Parameter
					(
						GoodsItemNumber,
						ItemDataID,
						ItemAmount,
						UsableDuration
					)
		WHERE	COALESCE(GoodsItemNumber, ItemDataID, ItemAmount, UsableDuration)	IS NOT NULL;
		SET	@itemCount = @@ROWCOUNT;
		IF	(@itemCount = 0)
			RAISERROR(N'there is no item parameter, ownerAccountId:%s, goodsId:0x%I64X', 16, @ERR_FAILURE, @ownerAccountIdStr, @GoodsID);
		
		IF	(@INIT_TRANCOUNT = 0)
			BEGIN TRAN
		
		BEGIN
			INSERT INTO dbo.WarehouseGoods (
				OwnerAccountID,
				GoodsID,
				GoodsNumber,
				SenderDescription,
				SenderMessage,
				PurchaseTime,
				RegistrationState,
				RegistrationTime,
				LastStateUpdateTime,
				RemainingItemCount
			)
			VALUES (
				@OwnerAccountID,
				@GoodsID,
				@GoodsNumber,
				@SenderDescription,
				@SenderMessage,
				@PurchaseTime,
				1,	-- Registration Ready
				@registrationTime,
				@registrationTime,
				@itemCount
			);
			SET	@NewLabelID	= SCOPE_IDENTITY();
			
			INSERT INTO dbo.WarehouseItem (
				LabelID,
				OwnerAccountID,
				OwnerWID,
				OwnerPCID,
				GoodsItemNumber,
				ItemDataID,
				ItemAmount,
				UsableDuration,
				ItemState,
				UserServiceType,
				RegistrationTime,
				LastStateUpdateTime
			)
			SELECT	@NewLabelID,
					@OwnerAccountID,
					NULL,
					NULL,
					GoodsItemNumber,
					ItemDataID,
					ItemAmount,
					UsableDuration,
					0,	--	Invalid State
					NULL,
					@registrationTime,
					@registrationTime
			FROM	@warehouseItem;
			SET	@resultCount = @@ROWCOUNT;
			IF	(@itemCount != @resultCount)
				RAISERROR(N'failed to register warehouse item; result count is not same, ownerAccountId:%s, goodsId:0x%I64X, itemCount:%d, resultCount:%d', 16, @ERR_FAILURE,
																						@ownerAccountIdStr, @GoodsID, @itemCount, @resultCount);
		END
		
		IF	(@INIT_TRANCOUNT = 0)
			COMMIT;
		
		RETURN	@ERR_SUCCESS;
	END TRY
	BEGIN CATCH
		IF	(XACT_STATE() != 0 AND @INIT_TRANCOUNT = 0)	BEGIN
			ROLLBACK;
			EXEC dbo.usp_LogError;
		END
		
		DECLARE	@ERR_NUMBER		INT				= ERROR_NUMBER(),
				@ERR_SEVERITY	INT				= ERROR_SEVERITY(),
				@ERR_STATE		INT				= ERROR_STATE(),
				@ERR_PROCEDURE	NVARCHAR(126)	= ERROR_PROCEDURE(),
				@ERR_MESSAGE	NVARCHAR(2048)	= ERROR_MESSAGE(),
				@ERR_CODE		INT				= @ERR_FAILURE;
		
		IF	(@ERR_NUMBER >= 50000)	BEGIN
			IF	(@ERR_PROCEDURE = OBJECT_NAME(@@PROCID))
				SET	@ERR_CODE = @ERR_STATE;
		END
		
		IF	(@@NESTLEVEL = 1)
			RAISERROR(N'[%s], %s', @ERR_SEVERITY, @ERR_CODE, @ERR_PROCEDURE, @ERR_MESSAGE);
		ELSE
			RAISERROR(@ERR_MESSAGE, @ERR_SEVERITY, @ERR_CODE);
		
		RETURN	@ERR_CODE;
	END CATCH
END


(1 rows affected)
